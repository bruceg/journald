Name: @PACKAGE@
Summary: User-space generic journalling daemon
Version: @VERSION@
Release: 1
Copyright: GPL
Group: Utilities/System
Source: http://em.ca/~bruceg/@PACKAGE@/@PACKAGE@-@VERSION@.tar.gz
BuildRoot: %{_tmppath}/@PACKAGE@-buildroot
URL: http://em.ca/~bruceg/@PACKAGE@/
Packager: Bruce Guenter <bruceg@em.ca>
Requires: bglibs >= 1.010

%description
Journald is a user-space standalone generic journalling daemon.  It
reads records from multiple clients and writes them to a single journal
in such a way that the client only sees a successful completion only
after the journal is guaranteed to be written to disk.

%prep
%setup

%build
make CFLAGS="$RPM_OPT_FLAGS" all

%install
rm -fr %{buildroot}
#make install_prefix=%{buildroot} bindir=%{_bindir} mandir=%{_mandir} install
rm -f conf_bin.c insthier.o installer instcheck
echo %{buildroot}%{_bindir} >conf-bin
make installer instcheck

mkdir -p %{buildroot}%{_bindir}
./installer
./instcheck

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc COPYING NEWS README TODO *.txt
%{_bindir}/*
