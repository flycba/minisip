%define name		libminisip
%define version		0.3.1
%define release		1

%define major		0
%define lib_name	%{name}%{major}

Summary: 		Application layer library to easily create GUI based apps
Name:			%{name}
Version:		%{version}
Release:		%{release}
Packager:		Johan Bilien <jobi@via.ecp.fr>
License:		GPL
URL:			http://www.minisip.org/libminisip/
Group:			System/Libraries
Source:			http://www.minisip.org/source/%{name}-%{version}.tar.gz
BuildRoot:		%_tmppath/%name-%version-%release-root

%description
Application layer library to easily create GUI based apps

%package -n %{lib_name}
Summary: 		Application layer library to easily create GUI based apps
Group:			System/Libraries
Provides:		%{name}
Requires:       	libmutil0 >= 0.3.1, libmnetutil0 >= 0.3.1, libmikeyl0 >= 0.3.1, libmsipl0 >= 0.3.1, 


%description -n %{lib_name}
Application layer library to easily create GUI based apps


%package -n %{lib_name}-devel
Summary: 		Development files for the libminisip library.
Group:          	Development/C
Provides:       	%name-devel
Requires:       	%{lib_name} = %{version}


%description -n %{lib_name}-devel
Application layer library to easily create GUI based apps

This package includes the development files (headers and static library)

%prep
%setup -q


%build
%configure
make

%install
%makeinstall

%clean
rm -rf %buildroot

%post -n %{lib_name} -p /sbin/ldconfig

%postun -n %{lib_name} -p /sbin/ldconfig
										

%files -n %{lib_name}
%defattr(-,root,root,-)
%doc AUTHORS README COPYING ChangeLog
%{_libdir}/*.so.*

%files -n %{lib_name}-devel
%defattr(-,root,root)
%doc COPYING
%{_libdir}/*a
%{_libdir}/*.so
%{_includedir}/*


%changelog
* Fri Mar 18 2005 Johan Bilien <jobi@via.ecp.fr>
- new upstream release
* Fri Feb 18 2005 Johan Bilien <jobi@via.ecp.fr>
- new upstream release
* Mon Nov 22 2004 Johan Bilien <jobi@via.ecp.fr>
- new upstream release
* Wed Jun 9 2004 Johan Bilien <jobi@via.ecp.fr>
- initial release
