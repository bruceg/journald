Name: @PACKAGE@
Summary: User-space generic journalling daemon
Version: @VERSION@
Release: 1
Copyright: GPL
Group: Utilities/System
Source: http://em.ca/~bruceg/@PACKAGE@/@PACKAGE@-@VERSION@.tar.gz
BuildRoot: /tmp/@PACKAGE@-buildroot
URL: http://em.ca/~bruceg/@PACKAGE@/
Packager: Bruce Guenter <bruceg@em.ca>

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
rm -fr $RPM_BUILD_ROOT
make install_prefix=$RPM_BUILD_ROOT bindir=%{_bindir} mandir=%{_mandir} install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc COPYING NEWS README TODO
%{_bindir}/*
