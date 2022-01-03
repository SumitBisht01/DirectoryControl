# DirectoryControl
The Directory control minifilter is an example for developers who intend to write filters that allows only the read operation and blocks other operation to the file inside a controled directory. 

# Usage
Download the latest release or compile the driver.

Enable test signing and reboot.

Right-click on the DirControl.inf file and click on Install.

Open an elevated command prompt again.

Load the driver with fltmc.exe with the load option:
fltmc load DirCtl

Execute following with command DCApp.exe "folderpath" (this is the folder path which need to be protected)

Protection to the dir path is activated.

Press any character to stop the directory protection.

Unload the driver with fltmc.exe with the unload option:
fltmc unload DirCtl
