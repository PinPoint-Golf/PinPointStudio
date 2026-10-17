@echo off
rem trunk-hw-record.cmd - step H1b of the trunk-IMU hardware session: start the installed app
rem with the raw IMU recorder on and the trunk roles offered (dev switches, no ceremony).
rem Recordings land in C:\PinPointStudio\scratch\trunk-hw\record-studio (= /mnt/swingdata/scratch
rem on the Mac). Quit the app to close the files. ASCII only.
set PINPOINT_IMU_RECORD_DIR=C:\PinPointStudio\scratch\trunk-hw\record-studio
set PINPOINT_DEV_TRUNK_MOUNTS=1
start "" "C:\Users\developer\AppData\Local\Programs\PinPointStudio\bin\PinPointStudio.exe"
