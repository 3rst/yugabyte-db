// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/docdb/intent_aware_iterator.h"

#include <future>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/dockv/intent.h"
#include "yb/docdb/intent_iterator.h"
#include "yb/docdb/iter_util.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/docdb/transaction_dump.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

using namespace std::literals;

DEFINE_RUNTIME_bool(use_fast_next_for_iteration, true,
                    "Whether intent aware iterator should use fast next feature.");

// Default value was picked intuitively, could try to find more suitable value in future.
DEFINE_RUNTIME_uint64(max_next_calls_while_skipping_future_records, 3,
                      "After number of next calls is reached this limit, use seek to find non "
                      "future record.");

namespace yb {
namespace docdb {

using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryTypeAsChar;
using dockv::SubDocKey;

namespace {

const char kKeyEntryTypeMaxByte = dockv::KeyEntryTypeAsChar::kMaxByte;

void AppendEncodedDocHt(const EncodedDocHybridTime& encoded_doc_ht, KeyBuffer* buffer) {
  buffer->PushBack(KeyEntryTypeAsChar::kHybridTime);
  buffer->Append(encoded_doc_ht.AsSlice());
}

template <Direction direction>
struct MoveIteratorHelper;

template <>
struct MoveIteratorHelper<Direction::kForward> {
  static const rocksdb::KeyValueEntry& Apply(BoundedRocksDbIterator* iter) {
    return iter->Next();
  }
};

template <>
struct MoveIteratorHelper<Direction::kBackward> {
  static const rocksdb::KeyValueEntry& Apply(BoundedRocksDbIterator* iter) {
    return iter->Prev();
  }
};

template <Direction direction>
const rocksdb::KeyValueEntry& MoveIterator(BoundedRocksDbIterator* iter) {
  return MoveIteratorHelper<direction>::Apply(iter);
}

// Given that key is well-formed DocDB encoded key, checks if it is an intent key for the same key
// as intent_prefix. If key is not well-formed DocDB encoded key, result could be true or false.
bool IsIntentForTheSameKey(Slice key, Slice intent_prefix) {
  return key.starts_with(intent_prefix) &&
         key.size() > intent_prefix.size() &&
         dockv::IntentValueType(key[intent_prefix.size()]);
}

std::string DebugDumpEntryToStr(const rocksdb::KeyValueEntry& entry) {
  if (!entry) {
    return "<INVALID>";
  }
  return Format("$0 => $1", DebugDumpKeyToStr(entry.key), entry.value.ToDebugHexString());
}

bool DebugHasHybridTime(Slice subdoc_key_encoded) {
  SubDocKey subdoc_key;
  CHECK(subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(subdoc_key_encoded).ok());
  return subdoc_key.has_hybrid_time();
}

} // namespace

std::string DebugDumpKeyToStr(Slice key) {
  auto result = SubDocKey::DebugSliceToStringAsResult(key);
  if (!result.ok()) {
    return key.ToDebugString();
  }
  return Format("$0 ($1)", key.ToDebugString(), *result);
}

IntentAwareIterator::IntentAwareIterator(
    const DocDB& doc_db,
    const rocksdb::ReadOptions& read_opts,
    const ReadOperationData& read_operation_data,
    const TransactionOperationContext& txn_op_context,
    rocksdb::Statistics* intentsdb_statistics)
    : read_time_(read_operation_data.read_time),
      encoded_read_time_(read_operation_data.read_time),
      txn_op_context_(txn_op_context),
      upperbound_(&kKeyEntryTypeMaxByte, 1),
      transaction_status_cache_(
          txn_op_context_, read_operation_data.read_time, read_operation_data.deadline) {
  VTRACE(1, __func__);
  VLOG(4) << "IntentAwareIterator, read_operation_data: " << read_operation_data.ToString()
          << ", txn_op_context: " << txn_op_context_;

  if (txn_op_context) {
    if (txn_op_context.txn_status_manager->MinRunningHybridTime() != HybridTime::kMax) {
      intent_iter_ = docdb::CreateRocksDBIterator(doc_db.intents,
                                                  doc_db.key_bounds,
                                                  docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                                  boost::none,
                                                  rocksdb::kDefaultQueryId,
                                                  nullptr /* file_filter */,
                                                  &intent_upperbound_,
                                                  intentsdb_statistics);
    } else {
      VLOG(4) << "No transactions running";
    }
  }
  // WARNING: Is is important for regular DB iterator to be created after intents DB iterator,
  // otherwise consistency could break, for example in following scenario:
  // 1) Transaction is T1 committed with value v1 for k1, but not yet applied to regular DB.
  // 2) Client reads v1 for k1.
  // 3) Regular DB iterator is created on a regular DB snapshot containing no values for k1.
  // 4) Transaction T1 is applied, k1->v1 is written into regular DB, intent k1->v1 is deleted.
  // 5) Intents DB iterator is created on an intents DB snapshot containing no intents for k1.
  // 6) Client reads no values for k1.
  iter_ = BoundedRocksDbIterator(doc_db.regular, read_opts, doc_db.key_bounds);
  iter_.UseFastNext(FLAGS_use_fast_next_for_iteration);
  VTRACE(2, "Created iterator");
}

void IntentAwareIterator::Seek(const dockv::DocKey &doc_key) {
  Seek(doc_key.Encode(), Full::kFalse);
}

void IntentAwareIterator::Seek(Slice key, Full full) {
  VLOG_WITH_FUNC(4) << "key: " << DebugDumpKeyToStr(key) << ", full: " << full;
  DOCDB_DEBUG_SCOPE_LOG(
      key.ToDebugString(),
      std::bind(&IntentAwareIterator::DebugDump, this));
  if (!status_.ok()) {
    return;
  }

  SeekTriggered();

  SkipFutureRecords<Direction::kForward>(ROCKSDB_SEEK(&iter_, key));
  if (intent_iter_.Initialized()) {
    if (!SetIntentUpperbound()) {
      return;
    }
    if (full) {
      seek_buffer_.Assign(key, StrongWriteSuffix(key));
      key = seek_buffer_.AsSlice();
    }
    SeekToSuitableIntent<Direction::kForward>(ROCKSDB_SEEK(&intent_iter_, key));
  }
  FillEntry();
}

void IntentAwareIterator::Next() {
  VLOG_WITH_FUNC(4);

  if (!status_.ok()) {
    return;
  }

  if (IsEntryRegular()) {
    SkipFutureRecords<Direction::kForward>(iter_.Next());
    FillEntry();
  }
}

void IntentAwareIterator::SeekForward(Slice key) {
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(key);
  DOCDB_DEBUG_SCOPE_LOG(
      DebugDumpKeyToStr(key),
      std::bind(&IntentAwareIterator::DebugDump, this));
  if (!status_.ok()) {
    return;
  }

  SeekTriggered();

  auto prefix_len = intent_iter_.Initialized() ? IntentPrepareSeek(key, StrongWriteSuffix(key)) : 0;
  SeekForwardRegular(key);
  IntentSeekForward(prefix_len);
  FillEntry();
}

size_t IntentAwareIterator::IntentPrepareSeek(Slice key, char suffix) {
  seek_buffer_.Assign(key, Slice(&suffix, 1));
  return seek_buffer_.size();
}

size_t IntentAwareIterator::IntentPrepareSeek(Slice key, Slice suffix) {
  VLOG_WITH_FUNC(4)
      << "key: " << DebugDumpKeyToStr(key) << ", suffix: " << suffix.ToDebugHexString();

  seek_buffer_.Assign(key, suffix);
  return key.size();
}

void IntentAwareIterator::IntentSeekForward(size_t prefix_len) {
  if (prefix_len == 0 || !status_.ok()) {
    return;
  }

  Slice prefix(seek_buffer_.data(), prefix_len);
  VLOG_WITH_FUNC(4) << "prefix: " << DebugDumpKeyToStr(prefix);

  if (!SetIntentUpperbound()) {
    return;
  }

  DOCDB_DEBUG_SCOPE_LOG(seek_buffer_.ToString(),
                        std::bind(&IntentAwareIterator::DebugDump, this));
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent &&
      resolved_intent_key_prefix_.CompareTo(prefix) >= 0) {
    VLOG(4) << __func__ << ", has suitable " << AsString(resolved_intent_state_) << " intent: "
            << DebugDumpKeyToStr(resolved_intent_key_prefix_);
    return;
  }

  if (VLOG_IS_ON(4)) {
    if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
      VLOG(4) << __func__ << ", has NOT suitable " << AsString(resolved_intent_state_)
              << " intent: " << DebugDumpKeyToStr(resolved_intent_key_prefix_);
    }

    if (intent_iter_.Valid()) {
      VLOG(4) << __func__ << ", current position: " << DebugDumpKeyToStr(intent_iter_.key());
    } else {
      HandleStatus(intent_iter_.status());
      VLOG(4) << __func__ << ", iterator invalid";
    }
  }

  SeekToSuitableIntent<Direction::kForward>(
      docdb::SeekForward(seek_buffer_.AsSlice(), &intent_iter_));
}

// TODO: If TTL rows are ever supported on subkeys, this may need to change appropriately.
// Otherwise, this function might seek past the TTL merge record, but not the original
// record for the actual subkey.
void IntentAwareIterator::SeekPastSubKey(Slice key) {
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(key);
  if (!status_.ok()) {
    return;
  }

  SeekTriggered();

  auto prefix_len = intent_iter_.Initialized()
      ? IntentPrepareSeek(key, KeyEntryTypeAsChar::kGreaterThanIntentType) : 0;
  SkipFutureRecords<Direction::kForward>(docdb::SeekPastSubKey(key, &iter_));
  IntentSeekForward(prefix_len);
  FillEntry();
}

void IntentAwareIterator::SeekOutOfSubDoc(KeyBytes* key_bytes) {
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(*key_bytes);
  if (!status_.ok()) {
    return;
  }

  SeekTriggered();

  auto prefix_len = intent_iter_.Initialized()
      ? IntentPrepareSeek(*key_bytes, KeyEntryTypeAsChar::kMaxByte) : 0;
  SkipFutureRecords<Direction::kForward>(docdb::SeekOutOfSubKey(key_bytes, &iter_));
  IntentSeekForward(prefix_len);
  FillEntry();
}

bool IntentAwareIterator::HasCurrentEntry() {
  return regular_entry_ || resolved_intent_state_ == ResolvedIntentState::kValid;
}

void IntentAwareIterator::SeekToLastDocKey() {
  SkipFutureRecords<Direction::kBackward>(iter_.SeekToLast());
  if (intent_iter_.Initialized()) {
    ResetIntentUpperbound();
    SeekToSuitableIntent<Direction::kBackward>(intent_iter_.SeekToLast());
  }
  if (HasCurrentEntry()) {
    SeekToLatestDocKeyInternal();
  } else {
    SeekTriggered();
  }
  FillEntry();
}

// If we reach a different key, stop seeking.
Result<FetchedEntry> IntentAwareIterator::NextFullValue() {
  auto key_data = VERIFY_RESULT_REF(Fetch());
  if (!key_data || !dockv::IsMergeRecord(key_data.value)) {
    return key_data;
  }

  key_data.write_time.Assign(EncodedDocHybridTime::kMin);
  auto key = key_data.key;
  const size_t key_size = key.size();
  bool found_record = false;
  bool found_something = false;

  while ((found_record = iter_.Valid()) &&  // as long as we're pointing to a record
         (key = iter_.key()).starts_with(key_data.key) &&  // with the same key we started with
         key[key_size] == KeyEntryTypeAsChar::kHybridTime && // whose key ends with a HT
         dockv::IsMergeRecord(
             key_data.value = iter_.value())) { // and whose value is a merge record
    iter_.Next(); // advance the iterator
  }
  HandleStatus(iter_.status());
  RETURN_NOT_OK(status_);

  if (found_record) {
    RETURN_NOT_OK(DocHybridTime::EncodedFromEnd(key, &key_data.write_time));
    key_data.key = key.WithoutSuffix(key_data.write_time.size());
    found_something = true;
  }

  found_record = false;
  if (intent_iter_.Initialized()) {
    while ((found_record = IsIntentForTheSameKey(intent_iter_.key(), key_data.key)) &&
           dockv::IsMergeRecord(key_data.value = intent_iter_.value())) {
      intent_iter_.Next();
    }
    if (found_record && !(key = intent_iter_.key()).empty()) {
      EncodedDocHybridTime doc_ht;
      RETURN_NOT_OK(DocHybridTime::EncodedFromEnd(key, &doc_ht));
      if (doc_ht >= key_data.write_time) {
        key_data.write_time = doc_ht;
        key_data.key = key.WithoutSuffix(doc_ht.size());
        found_something = true;
      }
    }
  }

  if (!found_something) {
    regular_entry_.Reset();
  }
  RETURN_NOT_OK(status_);
  return key_data;
}

bool IntentAwareIterator::PreparePrev(Slice key) {
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(key);

  // TODO(scanperf) allow fast next after reverse scan.
  // Fallback to regular Next if reverse scan was used.
  iter_.UseFastNext(false);

  ROCKSDB_SEEK(&iter_, key);

  if (iter_.Valid()) {
    SkipFutureRecords<Direction::kBackward>(iter_.Prev());
  } else {
    HandleStatus(iter_.status());
    SkipFutureRecords<Direction::kBackward>(iter_.SeekToLast());
  }

  if (intent_iter_.Initialized()) {
    ResetIntentUpperbound();
    ROCKSDB_SEEK(&intent_iter_, key);
    if (intent_iter_.Valid()) {
      SeekToSuitableIntent<Direction::kBackward>(intent_iter_.Prev());
    } else {
      HandleStatus(intent_iter_.status());
      if (!status_.ok()) {
        return false;
      }
      SeekToSuitableIntent<Direction::kBackward>(intent_iter_.SeekToLast());
    }
  }

  return HasCurrentEntry();
}

void IntentAwareIterator::PrevSubDocKey(const KeyBytes& key_bytes) {
  if (PreparePrev(key_bytes)) {
    SeekToLatestSubDocKeyInternal();
  }
  FillEntry();
}

void IntentAwareIterator::PrevDocKey(const dockv::DocKey& doc_key) {
  PrevDocKey(doc_key.Encode().AsSlice());
}

void IntentAwareIterator::PrevDocKey(Slice encoded_doc_key) {
  if (PreparePrev(encoded_doc_key)) {
    SeekToLatestDocKeyInternal();
  }
  FillEntry();
}

Slice IntentAwareIterator::LatestSubDocKey() {
  DCHECK(HasCurrentEntry())
      << "Expected regular_value(" << regular_entry_.value.ToDebugHexString()
      << ") || resolved_intent_state_(" << resolved_intent_state_
      << ") == ResolvedIntentState::kValid";
  return IsEntryRegular(/* descending */ true) ? iter_.key()
                                               : resolved_intent_key_prefix_.AsSlice();
}

void IntentAwareIterator::SeekToLatestSubDocKeyInternal() {
  auto subdockey_slice = LatestSubDocKey();

  // Strip the hybrid time and seek the slice.
  auto doc_ht = DocHybridTime::DecodeFromEnd(&subdockey_slice);
  if (!HandleStatus(doc_ht)) {
    return;
  }
  subdockey_slice.remove_suffix(1);
  Seek(subdockey_slice);
}

void IntentAwareIterator::SeekToLatestDocKeyInternal() {
  auto subdockey_slice = LatestSubDocKey();

  // Seek to the first key for row containing found subdockey.
  auto dockey_size = dockv::DocKey::EncodedSize(subdockey_slice, dockv::DocKeyPart::kWholeDocKey);
  if (!HandleStatus(dockey_size)) {
    return;
  }
  Seek(Slice(subdockey_slice.data(), *dockey_size));
}

bool IntentAwareIterator::IsEntryRegular(bool descending) {
  if (PREDICT_FALSE(!regular_entry_)) {
    return false;
  }
  if (resolved_intent_state_ == ResolvedIntentState::kValid) {
    return (iter_.key().compare(resolved_intent_sub_doc_key_encoded_.AsSlice()) < 0) != descending;
  }
  return true;
}

void IntentAwareIterator::Revalidate() {
  VLOG_WITH_FUNC(4);

  SkipFutureRecords<Direction::kForward>(iter_.Entry());
  if (intent_iter_.Initialized()) {
    if (!SetIntentUpperbound()) {
      return;
    }
    SkipFutureIntents();
  }
  FillEntry();
}

Result<const FetchedEntry&> IntentAwareIterator::Fetch() {
#ifndef NDEBUG
  need_fetch_ = false;
#endif

  RETURN_NOT_OK(status_);

  auto& result = entry_;
  if (result.valid) {
    VLOG(4) << "Fetched key " << DebugDumpKeyToStr(result.key)
            << ", kind: " << (result.same_transaction ? 'S' : (IsEntryRegular() ? 'R' : 'I'))
            << ", with time: " << result.write_time.ToString()
            << ", while read bounds are: " << read_time_;
  } else {
    VLOG(4) << "Fetched key <INVALID>";
  }

  YB_TRANSACTION_DUMP(
      Read, txn_op_context_ ? txn_op_context_.txn_status_manager->tablet_id() : TabletId(),
      txn_op_context_ ? txn_op_context_.transaction_id : TransactionId::Nil(),
      read_time_, CHECK_RESULT(result.write_time.Decode()), result.same_transaction,
      result.key.size(), result.key, result.value.size(), result.value);

  return &result;
}

void IntentAwareIterator::FillEntry() {
  bool has_intent = resolved_intent_state_ == ResolvedIntentState::kValid;
  if (regular_entry_) {
    if (!has_intent ||
        regular_entry_.key.compare(resolved_intent_sub_doc_key_encoded_.AsSlice()) < 0) {
      FillRegularEntry();
      return;
    }
    FillIntentEntry();
    return;
  }

  if (has_intent) {
    FillIntentEntry();
    return;
  }

  entry_.valid = false;
}

void IntentAwareIterator::FillRegularEntry() {
  entry_.valid = true;
  entry_.key = regular_entry_.key;
  if (!HandleStatus(DocHybridTime::EncodedFromEnd(entry_.key, &entry_.write_time))) {
    return;
  }
  entry_.key.remove_suffix(entry_.write_time.size() + 1);
  DCHECK_EQ(*entry_.key.end(), KeyEntryTypeAsChar::kHybridTime) << entry_.key.ToDebugString();
  entry_.same_transaction = false;
  entry_.value = regular_entry_.value;
  max_seen_ht_.MakeAtLeast(entry_.write_time);
}

void IntentAwareIterator::FillIntentEntry() {
  DCHECK_EQ(ResolvedIntentState::kValid, resolved_intent_state_);
  entry_.valid = true;
  entry_.key = resolved_intent_key_prefix_.AsSlice();
  entry_.write_time = GetIntentDocHybridTime(&entry_.same_transaction);
  entry_.value = resolved_intent_value_;
  max_seen_ht_.MakeAtLeast(resolved_intent_txn_dht_);
}

void IntentAwareIterator::SeekForwardRegular(Slice slice) {
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(slice);
  SkipFutureRecords<Direction::kForward>(docdb::SeekForward(slice, &iter_));
}

bool IntentAwareIterator::SatisfyBounds(Slice slice) {
  DCHECK(!upperbound_.empty());
  return slice.compare(upperbound_) <= 0;
}

void IntentAwareIterator::ProcessIntent() {
  auto decode_result = DecodeStrongWriteIntent(
      txn_op_context_, &intent_iter_, &transaction_status_cache_);
  if (!HandleStatus(decode_result)) {
    return;
  }
  const auto& decoded = *decode_result;
  VLOG(4) << "Intent decode: " << DebugIntentKeyToString(intent_iter_.key())
      << " => " << intent_iter_.value().ToDebugHexString() << ", result: " << decoded;
  DOCDB_DEBUG_LOG(
      "resolved_intent_txn_dht_: $0 value_time: $1 read_time: $2",
      resolved_intent_txn_dht_.ToString(),
      decoded.value_time.ToString(),
      read_time_.ToString());
  const auto& resolved_intent_time = decoded.same_transaction ? intent_dht_from_same_txn_
                                                              : resolved_intent_txn_dht_;
  VLOG(4) << "Intent decode: " << DebugIntentKeyToString(intent_iter_.key())
          << " => " << intent_iter_.value().ToDebugHexString() << ", result: " << decoded
          << ", resolved_intent_time: " << resolved_intent_time.ToString();
  // If we already resolved intent that is newer that this one, we should ignore current
  // intent because we are interested in the most recent intent only.
  if (decoded.value_time <= resolved_intent_time) {
    return;
  }

  // Ignore intent past read limit.
  if (decoded.value_time > decoded.MaxAllowedValueTime(encoded_read_time_)) {
    return;
  }

  if (resolved_intent_state_ == ResolvedIntentState::kNoIntent) {
    resolved_intent_key_prefix_.Reset(decoded.intent_prefix);
    if (!SatisfyBounds(decoded.intent_prefix)) {
      resolved_intent_state_ = ResolvedIntentState::kNoIntent;
    } else {
      resolved_intent_state_ = ResolvedIntentState::kValid;
    }
  }
  if (decoded.same_transaction) {
    intent_dht_from_same_txn_ = decoded.value_time;
    // We set resolved_intent_txn_dht_ to maximum possible time (time higher than read_time_.read
    // will cause read restart or will be ignored if higher than read_time_.global_limit) in
    // order to ignore intents/values from other transactions. But we save origin intent time into
    // intent_dht_from_same_txn_, so we can compare time of intents for the same key from the same
    // transaction and select the latest one.
    resolved_intent_txn_dht_.Assign(DocHybridTime(read_time_.read, kMaxWriteId));
  } else {
    resolved_intent_txn_dht_ = decoded.value_time;
  }
  resolved_intent_value_.Reset(decoded.intent_value);
}

void IntentAwareIterator::UpdateResolvedIntentSubDocKeyEncoded() {
  resolved_intent_sub_doc_key_encoded_.Assign(resolved_intent_key_prefix_.AsSlice());
  AppendEncodedDocHt(resolved_intent_txn_dht_, &resolved_intent_sub_doc_key_encoded_);
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_.AsSlice());
}

template<Direction direction>
void IntentAwareIterator::SeekToSuitableIntent(const rocksdb::KeyValueEntry& entry_ref) {
  DOCDB_DEBUG_SCOPE_LOG(/* msg */ "", std::bind(&IntentAwareIterator::DebugDump, this));
  resolved_intent_state_ = ResolvedIntentState::kNoIntent;
  resolved_intent_txn_dht_.Assign(EncodedDocHybridTime::kMin);
  intent_dht_from_same_txn_.Assign(EncodedDocHybridTime::kMin);

  // Find latest suitable intent for the first SubDocKey having suitable intents.
  const auto* entry = &entry_ref;
  while (*entry) {
    VLOG_WITH_FUNC(4) << "Entry: " << DebugDumpEntryToStr(*entry);

    if (entry->key[0] == KeyEntryTypeAsChar::kTransactionId) {
      // If the intent iterator ever enters the transaction metadata and reverse index region, skip
      // past it.
      switch (direction) {
        case Direction::kForward: {
          static const std::array<char, 1> kAfterTransactionId{
              KeyEntryTypeAsChar::kTransactionId + 1};
          static const Slice kAfterTxnRegion(kAfterTransactionId);
          entry = &intent_iter_.Seek(kAfterTxnRegion);
          break;
        }
        case Direction::kBackward:
          intent_upperbound_buffer_.Clear();
          intent_upperbound_buffer_.PushBack(KeyEntryTypeAsChar::kTransactionId);
          intent_upperbound_ = intent_upperbound_buffer_.AsSlice();
          // We are not calling RevalidateAfterUpperBoundChange here because it is only needed
          // during forward iteration, and is not needed immediately before a seek.
          entry = &intent_iter_.SeekToLast();
          break;
      }
      continue;
    }
    VLOG(4) << "Intent found: " << DebugIntentKeyToString(entry->key)
            << ", resolved state: " << yb::ToString(resolved_intent_state_);
    if (resolved_intent_state_ != ResolvedIntentState::kNoIntent &&
        // Only scan intents for the first SubDocKey having suitable intents.
        !IsIntentForTheSameKey(entry->key, resolved_intent_key_prefix_)) {
      break;
    }
    if (!SatisfyBounds(entry->key)) {
      break;
    }
    ProcessIntent();
    if (!status_.ok()) {
      LOG(WARNING) << "Entry: " << DebugDumpEntryToStr(*entry)
                   << " ProcessIntent failed: " << status_
                   << " TransactionOperationContext: " << txn_op_context_;
      return;
    }
    entry = &MoveIterator<direction>(&intent_iter_);
  }
  HandleStatus(intent_iter_.status());
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    UpdateResolvedIntentSubDocKeyEncoded();
  }
}

void IntentAwareIterator::DebugDump() {
  LOG(INFO) << ">> IntentAwareIterator dump";
  LOG(INFO) << "iter_.Valid(): " << iter_.Valid();
  if (iter_.Valid()) {
    LOG(INFO) << "iter_.key(): " << DebugDumpKeyToStr(iter_.key());
  } else if (!iter_.status().ok()) {
    LOG(INFO) << "iter_.status(): " << AsString(iter_.status());
    HandleStatus(iter_.status());
  }
  if (intent_iter_.Initialized()) {
    LOG(INFO) << "intent_iter_.Valid(): " << intent_iter_.Valid();
    if (intent_iter_.Valid()) {
      LOG(INFO) << "intent_iter_.key(): " << intent_iter_.key().ToDebugHexString();
    } else if (!intent_iter_.status().ok()) {
      LOG(INFO) << "intent_iter_.status(): " << AsString(intent_iter_.status());
      HandleStatus(intent_iter_.status());
    }
  }
  LOG(INFO) << "resolved_intent_state_: " << yb::ToString(resolved_intent_state_);
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    LOG(INFO) << "resolved_intent_sub_doc_key_encoded_: "
              << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_.AsSlice());
  }
  auto key_data = Fetch();
  if (key_data.ok()) {
    if (key_data) {
      LOG(INFO) << "key(): " << DebugDumpKeyToStr(key_data->key)
                << ", doc_ht: " << key_data->write_time.ToString();
    } else {
      LOG(INFO) << "Out of records";
    }
  } else {
    LOG(INFO) << "key(): fetch failed: " << key_data.status();
  }
  LOG(INFO) << "<< IntentAwareIterator dump";
}

Result<EncodedDocHybridTime> IntentAwareIterator::FindMatchingIntentRecordDocHybridTime(
    Slice key_without_ht) {
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(key_without_ht);

  IntentSeekForward(IntentPrepareSeek(key_without_ht, Slice()));
  RETURN_NOT_OK(status_);

  if (resolved_intent_state_ != ResolvedIntentState::kValid) {
    return EncodedDocHybridTime();
  }

  if (resolved_intent_key_prefix_.CompareTo(key_without_ht) == 0) {
    max_seen_ht_.MakeAtLeast(resolved_intent_txn_dht_);
    return GetIntentDocHybridTime();
  }
  return EncodedDocHybridTime();
}

Result<EncodedDocHybridTime> IntentAwareIterator::GetMatchingRegularRecordDocHybridTime(
    Slice key_without_ht) {
  size_t other_encoded_ht_size = VERIFY_RESULT(dockv::CheckHybridTimeSizeAndValueType(iter_.key()));
  Slice iter_key_without_ht = iter_.key();
  iter_key_without_ht.remove_suffix(1 + other_encoded_ht_size);
  if (key_without_ht == iter_key_without_ht) {
    EncodedDocHybridTime result;
    RETURN_NOT_OK(DocHybridTime::EncodedFromEnd(iter_.key(), &result));
    max_seen_ht_.MakeAtLeast(result);
    return result;
  }
  return EncodedDocHybridTime();
}

Result<HybridTime> IntentAwareIterator::FindOldestRecord(
    Slice key_without_ht, HybridTime min_hybrid_time) {
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(key_without_ht) << ", " << min_hybrid_time;
#define DOCDB_DEBUG
  DOCDB_DEBUG_SCOPE_LOG(
      DebugDumpKeyToStr(key_without_ht) + ", " + AsString(min_hybrid_time),
      std::bind(&IntentAwareIterator::DebugDump, this));
#undef DOCDB_DEBUG
  DCHECK(!DebugHasHybridTime(key_without_ht));

  if (!VERIFY_RESULT_REF(Fetch())) {
    VLOG_WITH_FUNC(4) << "Returning kInvalid";
    return HybridTime::kInvalid;
  }

  EncodedDocHybridTime encoded_min_hybrid_time(min_hybrid_time, kMaxWriteId);

  HybridTime result;
  if (intent_iter_.Initialized()) {
    auto intent_dht = VERIFY_RESULT(FindMatchingIntentRecordDocHybridTime(key_without_ht));
    VLOG_WITH_FUNC(4) << "Looking for Intent Record found ?  =  "
            << !intent_dht.empty();
    if (!intent_dht.empty() && intent_dht > encoded_min_hybrid_time) {
      result = VERIFY_RESULT(intent_dht.Decode()).hybrid_time();
      VLOG_WITH_FUNC(4) << " oldest_record_ht is now " << result;
    }
  } else {
    VLOG_WITH_FUNC(4) << "intent_iter_ not Initialized";
  }

  auto& seek_key_buffer = seek_buffer_;
  seek_key_buffer.Clear();
  seek_key_buffer.Reserve(key_without_ht.size() + 1 + encoded_min_hybrid_time.size());
  seek_key_buffer.Assign(key_without_ht);
  seek_key_buffer.PushBack(KeyEntryTypeAsChar::kHybridTime);
  seek_key_buffer.Append(encoded_min_hybrid_time.AsSlice());
  SeekForwardRegular(seek_key_buffer.AsSlice());
  RETURN_NOT_OK(status_);

  if (iter_.Valid()) {
    SkipFutureRecords<Direction::kForward>(iter_.Prev());
  } else {
    HandleStatus(iter_.status());
    RETURN_NOT_OK(status_);
    SkipFutureRecords<Direction::kForward>(iter_.SeekToLast());
  }

  if (regular_entry_) {
    auto regular_dht = VERIFY_RESULT(GetMatchingRegularRecordDocHybridTime(key_without_ht));
    VLOG(4) << "Looking for Matching Regular Record found   =  " << regular_dht.ToString();
    if (!regular_dht.empty()) {
      auto ht = VERIFY_RESULT(regular_dht.Decode()).hybrid_time();
      if (ht > min_hybrid_time) {
        result.MakeAtMost(ht);
      }
    }
  } else {
    VLOG(4) << "regular_value_ is empty";
  }
  VLOG(4) << "Returning " << result;
  return result;
}

Slice IntentAwareIterator::SetUpperbound(Slice upperbound) {
  VLOG_WITH_FUNC(4) << upperbound;
  auto result = upperbound_;
  upperbound_ = upperbound;
  return result;
}

Status IntentAwareIterator::FindLatestRecord(
    Slice key_without_ht,
    EncodedDocHybridTime* latest_record_ht,
    Slice* result_value) {
  if (!latest_record_ht) {
    return STATUS(Corruption, "latest_record_ht should not be a null pointer");
  }
  VLOG_WITH_FUNC(4) << DebugDumpKeyToStr(key_without_ht) << ", " << latest_record_ht->ToString();
  DOCDB_DEBUG_SCOPE_LOG(
      DebugDumpKeyToStr(key_without_ht) + ", " + AsString(latest_record_ht) + ", "
      + AsString(result_value),
      std::bind(&IntentAwareIterator::DebugDump, this));
  DCHECK(!DebugHasHybridTime(key_without_ht)) << DebugDumpKeyToStr(key_without_ht);

  if (!VERIFY_RESULT_REF(Fetch())) {
    return Status::OK();
  }

  bool found_later_intent_result = false;
  if (intent_iter_.Initialized()) {
    auto dht = VERIFY_RESULT(FindMatchingIntentRecordDocHybridTime(key_without_ht));
    if (!dht.empty() && dht > *latest_record_ht) {
      *latest_record_ht = dht;
      found_later_intent_result = true;
    }
  }

  auto& seek_key_buffer = seek_buffer_;
  seek_key_buffer.Clear();
  seek_key_buffer.Reserve(key_without_ht.size() + encoded_read_time_.global_limit.size() + 1);
  seek_key_buffer.Assign(key_without_ht);
  AppendEncodedDocHt(encoded_read_time_.global_limit, &seek_key_buffer);

  SeekForwardRegular(seek_key_buffer.AsSlice());
  // After SeekForwardRegular(), we need to call IsOutOfRecords() to skip future records and see if
  // the current key still matches the pushed prefix if any. If it does not, we are done.
  if (!VERIFY_RESULT_REF(Fetch())) {
    return Status::OK();
  }

  bool found_later_regular_result = false;
  if (regular_entry_) {
    auto dht = VERIFY_RESULT(GetMatchingRegularRecordDocHybridTime(key_without_ht));
    if (!dht.empty() && dht > *latest_record_ht) {
      *latest_record_ht = dht;
      found_later_regular_result = true;
    }
  }

  if (result_value) {
    if (found_later_regular_result) {
      *result_value = regular_entry_.value;
    } else if (found_later_intent_result) {
      *result_value = resolved_intent_value_;
    }
  }
  return Status::OK();
}

template <Direction direction>
void IntentAwareIterator::SkipFutureRecords(const rocksdb::KeyValueEntry& entry_ref) {
  VLOG_WITH_FUNC(4) << "direction: " << direction << ", entry: " << DebugDumpEntryToStr(entry_ref);

  size_t next_counter = 0;
  const auto* entry = &entry_ref;
  while (*entry) {
    Slice key = entry->key;
    if (!SatisfyBounds(key)) {
      VLOG_WITH_FUNC(4)
          << "Out of bounds: " << DebugDumpKeyToStr(key) << ", upperbound: "
          << DebugDumpKeyToStr(upperbound_);
      regular_entry_.Reset();
      return;
    }
    auto doc_ht_size = DocHybridTime::GetEncodedSize(key);
    if (!HandleStatus(doc_ht_size)) {
      LOG(DFATAL) << "Decode doc ht from key failed: " << status_ << ", key: "
                  << key.ToDebugHexString();
      return;
    }
    const auto encoded_doc_ht = key.Suffix(*doc_ht_size);
    auto value = entry->value;
    VLOG_WITH_FUNC(4)
        << "Checking for skip, type " << static_cast<KeyEntryType>(value[0])
        << ", encoded_doc_ht: " << DocHybridTime::DebugSliceToString(encoded_doc_ht)
        << " value: " << value.ToDebugHexString() << ", current key: "
        << DebugDumpKeyToStr(key);
    if (value.TryConsumeByte(KeyEntryTypeAsChar::kHybridTime)) {
      // Value came from a transaction, we could try to filter it by original intent time.
      // The logic here replicates part of the logic in
      // DecodeStrongWriteIntentResult:: MaxAllowedValueTime for intents that have been committed
      // and applied to regular RocksDB only. Note that here we are comparing encoded hybrid times,
      // so comparisons are reversed vs. the un-encoded case. If a value is found "invalid", it
      // can't cause a read restart. If it is found "valid", it will cause a read restart if it is
      // greater than read_time.read. That last comparison is done outside this function.
      auto max_allowed = value.compare(encoded_read_time_.local_limit.AsSlice()) > 0
          ? encoded_read_time_.global_limit.AsSlice()
          : encoded_read_time_.read.AsSlice();
      if (encoded_doc_ht.compare(max_allowed) > 0) {
        auto encoded_intent_doc_ht_result = DocHybridTime::EncodedFromStart(&value);
        if (!HandleStatus(encoded_intent_doc_ht_result)) {
          return;
        }
        regular_entry_ = { .key = entry->key, .value = value };
        return;
      }
    } else if (encoded_doc_ht.compare(encoded_read_time_.regular_limit()) > 0) {
      // If a value does not contain the hybrid time of the intent that wrote the original
      // transaction, then it either (a) originated from a single-shard transaction or (b) the
      // intent hybrid time has already been garbage-collected during a compaction because the
      // corresponding transaction's commit time (stored in the key) became lower than the history
      // cutoff. See the following commit for the details of this intent hybrid time GC.
      //
      // https://github.com/yugabyte/yugabyte-db/commit/26260e0143e521e219d93f4aba6310fcc030a628
      //
      // encoded_read_time_regular_limit_ is simply the encoded value of max(read_ht, local_limit).
      // The above condition
      //
      //   encoded_doc_ht.compare(encoded_read_time_regular_limit_) >= 0
      //
      // corresponds to the following in terms of decoded hybrid times (order is reversed):
      //
      //   commit_ht <= max(read_ht, local_limit)
      //
      // and the inverse of that can be written as
      //
      //   commit_ht > read_ht && commit_ht > local_limit
      //
      // The reason this is correct here is that in case (a) the event of writing a single-shard
      // record to the tablet would certainly be after our read transaction's start time in case
      // commit_ht > local_limit, so it can never cause a read restart. In case (b) we know that
      // commit_ht < history_cutoff and read_ht >= history_cutoff (by definition of history cutoff)
      // so commit_ht < read_ht, and in this case read restart is impossible regardless of the
      // value of local_limit.
      regular_entry_ = { .key = entry->key, .value = value };
      return;
    }
    if (direction == Direction::kForward &&
        ++next_counter >= FLAGS_max_next_calls_while_skipping_future_records) {
      if (encoded_read_time_.global_limit.AsSlice() > encoded_doc_ht) {
        KeyBuffer buffer(
            Slice(key.cdata(), encoded_doc_ht.cdata()), encoded_read_time_.global_limit.AsSlice());
        VLOG_WITH_FUNC(4)
            << "Seek because too many calls to next: " << DebugDumpKeyToStr(buffer.AsSlice());
        entry = &iter_.Seek(buffer.AsSlice());
      }
      next_counter = 0;
      continue;
    }
    VLOG_WITH_FUNC(4)
        << "Skipping because of time: " << DebugDumpKeyToStr(key) << ", read time: " << read_time_;
    entry = &MoveIterator<direction>(&iter_);
  }
  HandleStatus(iter_.status());
  regular_entry_.Reset();
}

void IntentAwareIterator::SkipFutureIntents() {
  if (!intent_iter_.Initialized() || !status_.ok()) {
    return;
  }
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    if (!SatisfyBounds(resolved_intent_key_prefix_.AsSlice())) {
      resolved_intent_state_ = ResolvedIntentState::kNoIntent;
    } else {
      resolved_intent_state_ = ResolvedIntentState::kValid;
    }
    return;
  }
  SeekToSuitableIntent<Direction::kForward>(intent_iter_.Entry());
}

bool IntentAwareIterator::SetIntentUpperbound() {
  VLOG_WITH_FUNC(4) << "regular_entry: " << DebugDumpEntryToStr(regular_entry_);

  if (regular_entry_) {
    // Strip ValueType::kHybridTime + DocHybridTime at the end of SubDocKey in iter_ and append
    // to upperbound with 0xff.
    Slice subdoc_key = regular_entry_.key;
    auto doc_ht_size = DocHybridTime::GetEncodedSize(subdoc_key);
    if (!HandleStatus(doc_ht_size)) {
      return false;
    }
    intent_upperbound_buffer_.Assign(subdoc_key.WithoutSuffix(1 + *doc_ht_size));
    intent_upperbound_buffer_.PushBack(KeyEntryTypeAsChar::kMaxByte);
    SyncIntentUpperbound();
    return status_.ok();
  } else {
    if (!status_.ok()) {
      return false;
    }
    // In case the current position of the regular iterator is invalid, set the exclusive intent
    // upperbound high to be able to find all intents higher than the last regular record.
    ResetIntentUpperbound();
  }
  return true;
}

void IntentAwareIterator::ResetIntentUpperbound() {
  if (upperbound_.empty()) {
    intent_upperbound_buffer_.Clear();
    intent_upperbound_buffer_.PushBack(KeyEntryTypeAsChar::kHighest);
  } else {
    intent_upperbound_buffer_.Assign(upperbound_);
  }
  SyncIntentUpperbound();
  VLOG(4) << "ResetIntentUpperbound = " << intent_upperbound_.ToDebugString();
}

void IntentAwareIterator::SyncIntentUpperbound() {
  intent_upperbound_ = intent_upperbound_buffer_.AsSlice();

  VLOG_WITH_FUNC(4) << "intent_upperbound: " << DebugDumpKeyToStr(intent_upperbound_);

  intent_iter_.RevalidateAfterUpperBoundChange();

  VLOG_WITH_FUNC(4) << "revalidated entry: " << DebugDumpEntryToStr(intent_iter_.Entry());

  HandleStatus(intent_iter_.status());
}

std::string IntentAwareIterator::DebugPosToString() {
  auto key = Fetch();
  if (!key.ok()) {
    return key.status().ToString();
  }
  if (!*key) {
    return "<OUT_OF_RECORDS>";
  }
  return DebugDumpKeyToStr(key->key);
}

Result<HybridTime> IntentAwareIterator::RestartReadHt() const {
  if (max_seen_ht_ <= encoded_read_time_.read) {
    return HybridTime::kInvalid;
  }
  auto decoded_max_seen_ht = VERIFY_RESULT(max_seen_ht_.Decode());
  VLOG(4) << "Restart read: " << decoded_max_seen_ht.hybrid_time() << ", original: " << read_time_;
  return decoded_max_seen_ht.hybrid_time();
}

HybridTime IntentAwareIterator::TEST_MaxSeenHt() const {
  return CHECK_RESULT(max_seen_ht_.Decode()).hybrid_time();
}

const EncodedDocHybridTime& IntentAwareIterator::GetIntentDocHybridTime(bool* same_transaction) {
  if (!intent_dht_from_same_txn_.is_min()) {
    if (same_transaction) {
      *same_transaction = true;
    }
    return intent_dht_from_same_txn_;
  }
  if (same_transaction) {
    *same_transaction = false;
  }
  return resolved_intent_txn_dht_;
}

EncodedReadHybridTime::EncodedReadHybridTime(const ReadHybridTime& read_time)
    : read(read_time.read, kMaxWriteId),
      local_limit(read_time.local_limit, kMaxWriteId),
      global_limit(read_time.global_limit, kMaxWriteId),
      in_txn_limit(read_time.in_txn_limit, kMaxWriteId),
      local_limit_gt_read(read_time.local_limit > read_time.read) {
}

bool IntentAwareIterator::HandleStatus(const Status& status) {
  if (status.ok()) {
    return true;
  }

  status_ = status;
  return false;
}

#ifndef NDEBUG
void IntentAwareIterator::DebugSeekTriggered() {
#if YB_INTENT_AWARE_ITERATOR_COLLECT_SEEK_STACK_TRACE
  DCHECK(!need_fetch_) << "Previous stack:\n" << last_seek_stack_trace_.Symbolize();
  last_seek_stack_trace_.Collect();
#else
  DCHECK(!need_fetch_);
#endif
  need_fetch_ = true;
}
#endif

}  // namespace docdb
}  // namespace yb
