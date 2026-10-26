@echo off
rem trunk-hw-record.cmd - step H1b of the trunk-IMU hardware session: start the installed app
rem with the raw IMU recorder on (the trunk mounts are always offered since W2.2).
rem Recordings land in C:\PinPointStudio\scratch\trunk-hw\record-studio (= /mnt/swingdata/scratch
rem on the Mac). Quit the app to close the files. ASCII only.
set PINPOINT_IMU_RECORD_DIR=C:\PinPointStudio\scratch\trunk-hw\record-studio
start "" "C:\Users\developer\AppData\Local\Programs\PinPointStudio\bin\PinPointStudio.exe"
