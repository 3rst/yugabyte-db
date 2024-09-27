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
#

# Utilities for finding available third-party release archives from GitHub.

import logging
import re

from typing import Optional, Tuple, Dict, Any, List, Set
from datetime import datetime

from autorepr import autorepr  # type: ignore
from github.GitRelease import GitRelease
from sys_detection import SHORT_OS_NAME_REGEX_STR

from yugabyte.string_util import none_to_empty_string
from yugabyte.os_versions import adjust_os_type


DOWNLOAD_URL_PREFIX = 'https://github.com/yugabyte/yugabyte-db-thirdparty/releases/download/'

# These were incorrectly used without the "clang" prefix to indicate various versions of Clang.
NUMBER_ONLY_VERSIONS_OF_CLANG = [str(i) for i in [12, 13, 14]]

COMPILER_TYPE_RE_STR = (r'(?:-(?P<compiler_type>(?:((?:gcc|clang|devtoolset-?)[a-z0-9.]+)|%s)))?' %
                        '|'.join(NUMBER_ONLY_VERSIONS_OF_CLANG))

ALLOWED_LTO_TYPES = ['thin', 'full']

# Linux distribution with the oldest glibc available to us. Third-party archives built on this
# OS can be used on newer Linux distributions, unless we need ASAN/TSAN.
PREFERRED_OS_TYPE = 'amzn2'


def get_arch_regex(index: int) -> str:
    """
    There are two places where the architecture could appear in the third-party archive release tag.
    We make them available under "architecture1" and "architecture2" capture group names.
    """
    arch_regex_str = '|'.join(['x86_64', 'aarch64', 'arm64'])
    return r'(?:-(?P<architecture%d>%s))?' % (index, arch_regex_str)


def get_archive_name_from_tag(tag: str) -> str:
    return f'yugabyte-db-thirdparty-{tag}.tar.gz'


TAG_RE_STR = ''.join([
    r'^v(?:(?P<branch_name>[0-9.]+)-)?',
    r'(?P<timestamp>[0-9]+)-',
    r'(?P<sha_prefix>[0-9a-f]+)',
    get_arch_regex(1),
    r'(?:-(?P<os>(?:%s)[a-z0-9.]*))' % SHORT_OS_NAME_REGEX_STR,
    get_arch_regex(2),
    r'(?:-(?P<is_linuxbrew1>linuxbrew))?',
    # "devtoolset" really means just "gcc" here. We should replace it with "gcc" in release names.
    # Also, "12", "13" and "14" were incorectly used instead of "clang13" in some release archive
    # names.
    COMPILER_TYPE_RE_STR,
    r'(?:-(?P<is_linuxbrew2>linuxbrew))?',
    r'(?:-(?:(?P<lto_type>%s)-lto))?' % '|'.join(ALLOWED_LTO_TYPES),
    r'$',
])
TAG_RE = re.compile(TAG_RE_STR)


class ThirdPartyReleaseBase:
    # The list of fields without the release tag. The tag is special because it includes the
    # timestamp, so by repeating a build on the same commit in yugabyte-db-thirdparty, we could get
    # multiple releases that have the same OS/architecture/compiler type/SHA but different tags.
    # Therefore we distinguish between "key with tag" and "key with no tag"
    KEY_FIELDS_NO_TAG = [
        'os_type', 'architecture', 'compiler_type', 'is_linuxbrew', 'sha', 'lto_type'
    ]
    KEY_FIELDS_WITH_TAG = KEY_FIELDS_NO_TAG + ['tag']

    os_type: str
    architecture: str
    compiler_type: str
    is_linuxbrew: bool
    sha: str
    tag: str
    lto_type: Optional[str]

    __str__ = __repr__ = autorepr(KEY_FIELDS_WITH_TAG)

    def as_dict(self) -> Dict[str, str]:
        return {
            k: getattr(self, k) for k in self.KEY_FIELDS_WITH_TAG
            if getattr(self, k) != ThirdPartyReleaseBase.get_default_field_value(k)
        }

    def get_sort_key(self, include_tag: bool = True) -> Tuple[str, ...]:
        return tuple(
            none_to_empty_string(getattr(self, k)) for k in
            (self.KEY_FIELDS_WITH_TAG if include_tag else self.KEY_FIELDS_NO_TAG))

    @staticmethod
    def get_default_field_value(field_name: str) -> Any:
        if field_name == 'lto_type':
            return None
        if field_name == 'is_linuxbrew':
            return False
        return ''


class SkipThirdPartyReleaseException(Exception):
    def __init__(self, msg: str) -> None:
        super().__init__(msg)


class GitHubThirdPartyRelease(ThirdPartyReleaseBase):
    github_release: GitRelease

    timestamp: str
    url: str
    branch_name: Optional[str]

    def __init__(self, github_release: GitRelease, target_commitish: Optional[str] = None) -> None:
        self.github_release = github_release
        self.sha = target_commitish or self.github_release.target_commitish

        tag = self.github_release.tag_name
        if tag.endswith('-snyk-scan'):
            raise SkipThirdPartyReleaseException(f"Skipping a tag ending with '-snyk-scan': {tag}")

        tag_match = TAG_RE.match(tag)
        if not tag_match:
            logging.info(f"Full regular expression for release tags: {TAG_RE_STR}")
            raise ValueError(f"Could not parse tag: {tag}, does not match regex: {TAG_RE_STR}")

        group_dict = tag_match.groupdict()

        sha_prefix = tag_match.group('sha_prefix')
        if not self.sha.startswith(sha_prefix):
            msg = (f"SHA prefix {sha_prefix} extracted from tag {tag} is not a prefix of the "
                   f"SHA corresponding to the release/tag: {self.sha}. Skipping.")
            raise SkipThirdPartyReleaseException(msg)

        self.timestamp = group_dict['timestamp']
        self.os_type = adjust_os_type(group_dict['os'])

        arch1 = group_dict['architecture1']
        arch2 = group_dict['architecture2']
        if arch1 is not None and arch2 is not None and arch1 != arch2:
            raise ValueError("Contradicting values of arhitecture in tag '%s'" % tag)
        self.architecture = arch1 or arch2
        self.is_linuxbrew = (bool(group_dict.get('is_linuxbrew1')) or
                             bool(group_dict.get('is_linuxbrew2')))

        compiler_type = group_dict.get('compiler_type')
        if compiler_type is None and self.os_type == 'macos':
            compiler_type = 'clang'
        if compiler_type is None and self.is_linuxbrew:
            compiler_type = 'gcc'
        if compiler_type in NUMBER_ONLY_VERSIONS_OF_CLANG:
            assert isinstance(compiler_type, str)
            compiler_type == 'clang' + compiler_type

        if compiler_type is None:
            raise ValueError(
                f"Could not determine compiler type from tag {tag}. Matches: {group_dict}.")
        compiler_type = compiler_type.strip('-')
        self.tag = tag
        self.compiler_type = compiler_type

        branch_name = group_dict.get('branch_name')
        if branch_name is not None:
            branch_name = branch_name.rstrip('-')
        self.branch_name = branch_name

        self.lto_type = group_dict.get('lto_type')

    def validate_url(self) -> bool:
        asset_urls = [asset.browser_download_url for asset in self.github_release.get_assets()]

        if len(asset_urls) != 2:
            logging.warning(
                "Expected to find exactly two asset URLs for a release "
                "(one for the .tar.gz, the other for the checksum), "
                f"but found {len(asset_urls)}: {asset_urls}")
            return False

        non_checksum_urls = [url for url in asset_urls if not url.endswith('.sha256')]
        assert len(non_checksum_urls) == 1
        self.url = non_checksum_urls[0]
        if not self.url.startswith(DOWNLOAD_URL_PREFIX):
            logging.warning(
                f"Expected archive download URL to start with {DOWNLOAD_URL_PREFIX}, found "
                f"{self.url}")
            return False

        url_suffix = self.url[len(DOWNLOAD_URL_PREFIX):]
        url_suffix_components = url_suffix.split('/')
        assert len(url_suffix_components) == 2

        archive_basename = url_suffix_components[1]
        expected_basename = get_archive_name_from_tag(self.tag)
        if archive_basename != expected_basename:
            logging.warning(
                f"Expected archive name based on tag: {expected_basename}, "
                f"actual name: {archive_basename}, url: {self.url}")
            return False

        return True

    def is_consistent_with_yb_version(self, yb_version: str) -> bool:
        return (self.branch_name is None or
                yb_version.startswith((self.branch_name + '.', self.branch_name + '-')))

    def should_skip_as_too_os_specific(self) -> bool:
        """
        Certain build types of specific OSes could be skipped because we can use our "preferred OS
        type", the supported Linux distribution with the oldest glibc version, instead. We can do
        that in cases we know we don't need to run ASAN/TSAN. We know that we don't use ASAN/TSAN
        on aarch64 or for LTO builds as of 11/07/2022. Also we don't skip Linuxbrew builds or GCC
        builds.
        """
        return (
            self.os_type != PREFERRED_OS_TYPE and
            self.compiler_type.startswith('clang') and
            # We handle Linuxbrew builds in a special way, e.g. they could be built on AlmaLinux 8.
            not self.is_linuxbrew and
            # We don't run ASAN/TSAN on aarch64 or with LTO yet.
            (self.architecture == 'aarch64' or self.lto_type is not None)
        )


class ReleaseGroup:
    sha: str
    releases: List[GitHubThirdPartyRelease]
    creation_timestamps: List[datetime]

    def __init__(self, sha: str) -> None:
        self.sha = sha
        self.releases = []
        self.creation_timestamps = []

    def add_release(self, release: GitHubThirdPartyRelease) -> None:
        if release.sha != self.sha:
            raise ValueError(
                f"Adding a release with wrong SHA. Expected: {self.sha}, got: "
                f"{release.sha}.")
        self.releases.append(release)
        self.creation_timestamps.append(release.github_release.created_at)

    def get_max_creation_timestamp(self) -> datetime:
        return max(self.creation_timestamps)

    def get_min_creation_timestamp(self) -> datetime:
        return min(self.creation_timestamps)
