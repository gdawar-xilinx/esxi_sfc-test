*Prerequisites*:
Windows:
--------
1.) Windows MinGW compiler to build a dll. (http://www.codeblocks.org/home)
2.) Python 2.7 ( https://www.python.org/download/releases/2.7/)
3.) pscp.exe    (https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html)
4.) wget-1.11.4.1 (http://gnuwin32.sourceforge.net/packages/wget.htm)

Please install all above binaries and set thier respective paths in environment variable.

Linux:
------
1.) Run below commands first on the on develoment vm you want to use :-
    a.) ssh-keygen -t rsa # ENTER to every field
    b.) ssh-copy-id <user@remote_machine> ex. bsingh@chisel.uk.solarflarecom.com

Command Usage:
--------------

1.) windows:
    a.) Run command "gcc -shared fetch_image.h -o libfetch_image.dll fetch_image.c" to create a dll.
    b.) python fetch_firmware_image.py <output_dir> <fw_family_version> <username> <machinename> -t <v5 tag/branch>

For windows a Password prompt will come, so that you can connect to the remote machine.

2.) Linux:
    a.) Run "make" to create a ".so"
    b.) python fetch_firmware_image.py <output_dir> <fw_family_version> <username> <machinename> -t <v5 tag/branch> -c <true>


*Note:-

1.) "-c" option will be only used when we have to create a firmware_images vib.
2.) descriptor.xml needs to be copied at "root/stagedir" to create the vib.

