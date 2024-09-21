# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

from typing import List, Optional, Any

import argparse
import logging
import os
import sys

from sys_detection import local_sys_conf, is_macos

from yugabyte import arg_util

from yugabyte.common_util import (
    to_yaml_str,
    arg_str_to_bool,
)

from yugabyte.thirdparty_archives_metadata import (
    THIRDPARTY_ARCHIVES_REL_PATH,
    MetadataItem,
)

from yugabyte.thirdparty_releases import (
    ALLOWED_LTO_TYPES,
    NUMBER_ONLY_VERSIONS_OF_CLANG,
)

from yugabyte.os_versions import is_compatible_os
from yugabyte.string_util import matches_maybe_empty

from yugabyte.thirdparty_releases import PREFERRED_OS_TYPE

from yugabyte.thirdparty_archives_metadata import MetadataUpdater

from yugabyte import inline_thirdparty


def parse_args() -> argparse.Namespace:
    # TODO: refactor this to use submodules.

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--github-token-file',
        help='Read GitHub token from this file. Authenticated requests have a higher rate limit. '
             'If this is not specified, we will still use the GITHUB_TOKEN environment '
             'variable. The YB_GITHUB_TOKEN_FILE_PATH environment variable, if set, will be used '
             'as the default value of this argument.',
        default=os.getenv('YB_GITHUB_TOKEN_FILE_PATH'))
    parser.add_argument(
        '--update', '-u', action='store_true',
        help=f'Update the third-party archive metadata in in {THIRDPARTY_ARCHIVES_REL_PATH}.')
    parser.add_argument(
        '--list-compilers',
        action='store_true',
        help='List compiler types available for the given OS and architecture')
    parser.add_argument(
        '--get-sha1',
        action='store_true',
        help='Show the Git SHA1 of the commit to use in the yugabyte-db-thirdparty repo '
             'in case we are building the third-party dependencies from scratch.')
    parser.add_argument(
        '--save-thirdparty-url-to-file',
        help='Determine the third-party archive download URL for the combination of criteria, '
             'including the compiler type, and write it to the file specified by this argument.')
    parser.add_argument(
        '--compiler-type',
        help='Compiler type, to help us decide which third-party archive to choose. '
             'The default value is determined by the YB_COMPILER_TYPE environment variable.',
        default=os.getenv('YB_COMPILER_TYPE'))
    parser.add_argument(
        '--os-type',
        help='Operating system type, to help us decide which third-party archive to choose. '
             'The default value is determined automatically based on the current OS.')
    parser.add_argument(
        '--architecture',
        help='Machine architecture, to help us decide which third-party archive to choose. '
             'The default value is determined automatically based on the current platform.')
    parser.add_argument(
        '--is-linuxbrew',
        help='Whether the archive should be based on Linuxbrew.',
        type=arg_str_to_bool,
        default=None)
    parser.add_argument(
        '--verbose',
        help='Verbose debug information')
    parser.add_argument(
        '--tag-filter-regex',
        help='Only look at tags satisfying this regular expression.')
    parser.add_argument(
        '--lto',
        choices=ALLOWED_LTO_TYPES,
        help='Specify link-time optimization type.')
    parser.add_argument(
        '--also-use-commit',
        nargs='+',
        type=arg_util.sha1_regex_arg_type,
        help='One or more Git commits in the yugabyte-db-thirdparty repository that we should '
             'find releases for, in addition to the most recent commit in that repository that is '
             'associated with any of the releases. For use with --update.')
    parser.add_argument(
        '--allow-older-os',
        help='Allow using third-party archives built for an older compatible OS, such as CentOS 7.'
             'This is typically OK, as long as no runtime libraries for e.g. ASAN or UBSAN '
             'need to be used, which have to be built for the exact same version of OS.',
        action='store_true')
    parser.add_argument(
        '--override-default-sha',
        type=arg_util.sha1_regex_arg_type,
        help='Use the given SHA at the top of the generated third-party archives file.')
    parser.add_argument(
        '--sync-inline-thirdparty',
        action='store_true',
        help='Sync the inline third-party dependencies directory (%s) with the metadata in %s.' % (
            inline_thirdparty.INLINE_THIRDPARTY_SRC_DIR,
            inline_thirdparty.INLINE_THIRDPARTY_CONFIG_PATH
        ))

    if len(sys.argv) == 1:
        parser.print_help(sys.stderr)
        sys.exit(1)

    return parser.parse_args()


def filter_for_os(archive_candidates: List[MetadataItem], os_type: str) -> List[MetadataItem]:
    filtered_exactly = [
        candidate for candidate in archive_candidates if candidate.os_type == os_type
    ]
    if filtered_exactly:
        return filtered_exactly
    return [
        candidate for candidate in archive_candidates
        # is_compatible_os does not take into account that some code built on CentOS 7 might run
        # on AlmaLinux 8, etc. It only takes into account the equivalence of various flavors of RHEL
        # compatible OSes.
        if is_compatible_os(candidate.os_type, os_type)
    ]


def get_compilers(
        metadata_items: List[MetadataItem],
        os_type: Optional[str],
        architecture: Optional[str],
        is_linuxbrew: Optional[bool],
        lto: Optional[str],
        allow_older_os: bool) -> list:
    if not os_type:
        os_type = local_sys_conf().short_os_name_and_version()
    if not architecture:
        architecture = local_sys_conf().architecture
    preferred_os_type: Optional[str] = None
    if (allow_older_os and
            not is_linuxbrew and
            os_type != PREFERRED_OS_TYPE and
            not os_type.startswith('mac')):
        preferred_os_type = PREFERRED_OS_TYPE

    candidates: List[MetadataItem] = [
        metadata_item
        for metadata_item in metadata_items
        if metadata_item.architecture == architecture and
        matches_maybe_empty(metadata_item.lto_type, lto)
    ]

    os_candidates = filter_for_os(candidates, os_type)
    if preferred_os_type:
        candidates_for_preferred_os_type = filter_for_os(candidates, preferred_os_type)
        os_candidates.extend(candidates_for_preferred_os_type)
    if is_linuxbrew is not None:
        os_candidates = [
            candidate for candidate in os_candidates
            if candidate.is_linuxbrew == is_linuxbrew
        ]

    compilers = sorted(set([metadata_item.compiler_type for metadata_item in os_candidates]))

    return compilers


def compiler_type_matches(a: str, b: str) -> bool:
    '''
    >>> compiler_type_matches('clang10', 'clang10')
    True
    >>> compiler_type_matches('clang14', 'gcc11')
    False
    >>> compiler_type_matches('12', 'clang12')
    True
    >>> compiler_type_matches('clang12', '12')
    True
    >>> compiler_type_matches('clang12', '14')
    False
    '''
    if a == b:
        return True
    if a > b:
        return compiler_type_matches(b, a)
    return a in NUMBER_ONLY_VERSIONS_OF_CLANG and b == 'clang' + a


def get_third_party_release(
        available_archives: List[MetadataItem],
        compiler_type: str,
        os_type: Optional[str],
        architecture: Optional[str],
        is_linuxbrew: Optional[bool],
        lto: Optional[str],
        allow_older_os: bool) -> MetadataItem:
    if not os_type:
        os_type = local_sys_conf().short_os_name_and_version()
    preferred_os_type: Optional[str] = None
    if (allow_older_os and
            not is_linuxbrew and
            os_type != PREFERRED_OS_TYPE and
            not os_type.startswith('mac')):
        preferred_os_type = PREFERRED_OS_TYPE

    if not architecture:
        architecture = local_sys_conf().architecture

    needed_compiler_type = compiler_type

    candidates: List[Any] = [
        archive for archive in available_archives
        if compiler_type_matches(archive.compiler_type, needed_compiler_type) and
        archive.architecture == architecture and
        matches_maybe_empty(archive.lto_type, lto)
    ]

    if is_linuxbrew is not None:
        candidates = [
            candidate for candidate in candidates
            if candidate.is_linuxbrew == is_linuxbrew
        ]

    if is_linuxbrew is None or not is_linuxbrew or len(candidates) > 1:
        # If a Linuxbrew archive is requested, we don't have to filter by OS, because archives
        # should be OS-independent. But still do that if we have more than one candidate.
        #
        # Also, if we determine that we would rather use a "preferred OS type" (an old version of
        # Linux that allows us to produce "universal packages"), we try to use it first.

        filtered_for_os = False
        if preferred_os_type:
            candidates_for_preferred_os_type = filter_for_os(candidates, preferred_os_type)
            if candidates_for_preferred_os_type:
                candidates = candidates_for_preferred_os_type
                filtered_for_os = True

        if not filtered_for_os:
            candidates = filter_for_os(candidates, os_type)

    if len(candidates) == 1:
        return candidates[0]

    if candidates:
        i = 1
        for candidate in candidates:
            logging.warning("Third-party release archive candidate #%d: %s", i, candidate)
            i += 1
        wrong_count_str = 'more than one'
    else:
        if (is_macos() and
                os_type == 'macos' and
                compiler_type.startswith('clang') and
                compiler_type != 'clang'):
            return get_third_party_release(
                available_archives=available_archives,
                compiler_type='clang',
                os_type=os_type,
                architecture=architecture,
                is_linuxbrew=False,
                lto=lto,
                allow_older_os=False)
        logging.info(f"Available release archives:\n{to_yaml_str(available_archives)}")
        wrong_count_str = 'no'

    raise ValueError(
        f"Found {wrong_count_str} third-party release archives to download for OS type "
        f"{os_type}, compiler type matching {compiler_type}, architecture {architecture}, "
        f"is_linuxbrew={is_linuxbrew}. See more details above.")


def update_thirdparty_dependencies(args: argparse.Namespace) -> None:
    updater = MetadataUpdater(
        github_token_file_path=args.github_token_file,
        tag_filter_regex_str=args.tag_filter_regex,
        also_use_commits=args.also_use_commit,
        override_default_sha=args.override_default_sha)
    updater.update_archive_metadata_file()
