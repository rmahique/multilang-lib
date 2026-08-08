%global srcname multilang

# build-rpm.sh passes --define "version ..." (packaging/compute-version.sh's
# output) for every commit build; 0.1.0 here is only a fallback for anyone
# invoking rpmbuild directly against this spec without going through the
# script.
%{!?version: %global version 0.1.0}

Name:           python3-%{srcname}
Version:        %{version}
Release:        1%{?dist}
Summary:        Reusable multi-language string storage library

License:        GPL-3.0-or-later
URL:            https://example.invalid/%{srcname}
Source0:        %{srcname}-%{version}.tar.gz

BuildArch:      noarch

%if 0%{?suse_version}
# Both openSUSE Leap 15 and openSUSE Tumbleweed set %%suse_version, and
# both build via this same pip-wheel mechanism (self-contained, doesn't
# depend on Fedora's pyproject-rpm-macros package being available on
# SUSE). They differ only in which python3 is usable directly:
#
#   - Leap 15's default python3 is 3.6, too old for this project's
#     requires-python >=3.9 -- build-rpm.sh passes
#     --define "leap15_python_workaround 1" for that job only, which
#     selects the versioned python310 package here.
#   - Tumbleweed (rolling release) already ships a current default
#     python3, so it uses that directly, same as Fedora/RHEL would if
#     they took this branch.
#
# Neither python310-pip nor python310-pytest exist in Leap 15's repos, so
# %%check below skips gracefully rather than depending on pip succeeding
# at build time -- real RPM build farms (mock/OBS) run without network
# access, so a pip install inside %%check wouldn't be reliable there even
# if it works in an ad-hoc container with internet. Tumbleweed's default
# python3-pip/python3-pytest are expected to be installable, so %%check
# runs for real there.
%if 0%{?leap15_python_workaround}
%global suse_python python3.10
%global suse_python_pkg python310
%else
%global suse_python python3
%global suse_python_pkg python3
%endif
BuildRequires:  %{suse_python_pkg}
BuildRequires:  %{suse_python_pkg}-devel
BuildRequires:  %{suse_python_pkg}-setuptools
# %%python3_sitelib isn't defined without the base python-rpm-macros
# package (which this spec deliberately doesn't pull in on the
# leap15_python_workaround path, since it would point at the default
# python3 = 3.6, not python310) -- ask the target python's own sysconfig
# for the exact path it will actually install into instead of guessing
# lib vs lib64.
%global python3_sitelib %(%{suse_python} -c "import sysconfig; print(sysconfig.get_path('purelib', vars={'base': '/usr', 'platbase': '/usr'}))" 2>/dev/null || echo /usr/lib/%{suse_python}/site-packages)
%else
# Fedora / RHEL / CentOS Stream: the modern PEP 517 build/install macros
# (%%pyproject_wheel / %%pyproject_install), not the legacy setup.py-based
# %%py3_build / %%py3_install -- this project has no setup.py.
BuildRequires:  python3-devel
BuildRequires:  python3-setuptools
BuildRequires:  python3-pytest
BuildRequires:  pyproject-rpm-macros
%endif

%if 0%{?suse_version}
# rpm's own dependency generator already adds Requires: python(abi) = X.Y
# from the installed files; requiring the generic "python3" package here
# would make the built package impossible to install on a system that
# only has the versioned python310 package (the leap15_python_workaround
# case), so require whichever package name this build actually targeted.
Requires:       %{suse_python_pkg}
%else
Requires:       python3
%endif

%description
Provides retrieve_data, insert_data, and db_connector for storing and
retrieving translated strings, keyed by string_id + BCP 47 language_id,
against SQLite, PostgreSQL, or MySQL/MariaDB. SQLite support needs no
extra package; see the -postgres and -mysql subpackages for the other
backends.

%package -n python3-%{srcname}-postgres
Summary:        PostgreSQL backend support for python3-%{srcname}
Requires:       python3-%{srcname} = %{version}-%{release}
Requires:       python3-psycopg2

%description -n python3-%{srcname}-postgres
Pulls in psycopg2 so %{srcname}'s PostgreSQL backend can be used.

%package -n python3-%{srcname}-mysql
Summary:        MySQL/MariaDB backend support for python3-%{srcname}
Requires:       python3-%{srcname} = %{version}-%{release}
Requires:       python3-PyMySQL

%description -n python3-%{srcname}-mysql
Pulls in PyMySQL so %{srcname}'s MySQL/MariaDB backend can be used.

%prep
%autosetup -n %{srcname}-%{version}

%if 0%{?suse_version}
%build
# ensurepip: bootstraps pip from CPython's own bundled wheel -- no
# network needed, unlike `pip install <anything from PyPI>`. Then build
# the wheel with pip's own built-in PEP 517 support (--no-build-isolation
# so it uses the already-installed %{suse_python_pkg}-setuptools instead
# of trying to fetch an isolated build environment), avoiding any
# dependency on the separate `build` PyPI package, which also isn't
# packaged for python310 here (Tumbleweed's default python3 does have it,
# but this stays uniform across both SUSE flavors).
%{suse_python} -m ensurepip --upgrade >/dev/null 2>&1
%{suse_python} -m pip wheel . --no-deps --no-build-isolation --wheel-dir dist

%install
%{suse_python} -m pip install dist/*.whl --no-deps --root=%{buildroot} --prefix=/usr
%else
%build
%pyproject_wheel

%install
%pyproject_install
%pyproject_save_files %{srcname}
%endif

%check
# test_conformance.py reaches outside this package to a sibling
# conformance/cases.json that isn't part of this source tarball (see
# ../debian/rules for the same reasoning) -- packaging only needs to
# confirm this package installs and its own unit/validation logic works.
%if 0%{?suse_version}
# Neither python310-pytest nor python310-pip is packaged in Leap 15's
# repos (see the BuildRequires comment above), so pytest may genuinely
# not be installable offline there -- Tumbleweed's default python3-pytest
# is expected to be available. Either way: run the tests if pytest
# happens to be importable, and skip with a clear message rather than
# failing the whole package build over a test-runner that was never a
# hard dependency of the package itself.
if PYTHONPATH=%{buildroot}%{python3_sitelib} %{suse_python} -c "import pytest" >/dev/null 2>&1; then
    PYTHONPATH=%{buildroot}%{python3_sitelib} %{suse_python} -m pytest tests/ -v --ignore=tests/test_conformance.py
else
    echo "pytest not available for %{suse_python} on this system; skipping %%check"
fi
%else
PYTHONPATH=%{buildroot}%{python3_sitelib} %{__python3} -m pytest tests/ -v --ignore=tests/test_conformance.py
%endif

%if 0%{?suse_version}
%files
%license LICENSE
%doc README.md
%{python3_sitelib}/%{srcname}/
%{python3_sitelib}/%{srcname}-%{version}*.dist-info/
%else
# %%pyproject_save_files (above) recorded the exact install manifest, so
# this doesn't need to guess at egg-info vs dist-info layout.
%files -f %{pyproject_files}
%license LICENSE
%doc README.md
%endif

%changelog
* Wed Jul 29 2026 Raúl Mahiques <claude.ia@raulmahiques.com> - 0.1.0-1
- Initial release: db_connector, retrieve_data, insert_data against
  SQLite, PostgreSQL, and MySQL/MariaDB.
