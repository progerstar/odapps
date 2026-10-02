@echo off&:: by Alex Dragokas -05.01.13-
SetLocal EnableDelayedExpansion EnableExtensions
::Name of a watching processes (separeted by ,)
Set ProcNames=example.exe,example2.exe
::Set ProcNames=iw5mp_server.exe
::Delay (ms.) before the next check
Set /A DelayNewCheck=5000
::Delay (ms.) before starting a process after shutting down because of "not responding" status
Set DelayStartProcess=3000
::Setting Priority of Application to Run with (1 to 6):
::1 - LOW, 2 - BELOWNORMAL, 3 - NORMAL, 4 - ABOVENORMAL, 5 - HIGH, 6 - REALTIME
Set Priority=3
::Start the process in Minimize mode? (1 - Yes, 2 - No)
Set Minimize=1
 
::The time (ms.) while we checking if a process really not responding all this time
Set /A BugTime=10000
::How many times we make a series of chekings (during %BugTime%)
Set /A BugChecks=10
 
Set /A BugIntervals=%BugChecks% - 1
Set /A BugIntervalsDelay=%BugTime% / %BugIntervals%
Call :PriorityEnum %Priority% Priority
if %Minimize%==1 (set Minimize=/MIN ) else (set Minimize=)
Set ProcNames="%ProcNames:,=" "%"
 
:begin
For %%A in (%ProcNames%) do call :WatchSingleProcess "%%~A"
Call :Delay %DelayNewCheck%
Goto :begin
 
 
:WatchSingleProcess %1-Name_of_Process
Set Process=%~1
Call :IsResponding "%Process%"
Set PID=%errorlevel%
if %PID% neq 0 (
  <nul set /p test=Found Bug in "%Process%" PID=%PID%
  for /L %%C in (1,1,%BugChecks%) do (
    Call :IsResponding "%Process%"
    if !errorlevel! neq 0 (
        <nul set /p test=.
        if %%C==%BugChecks% (Call :Restart "%PID%" "%Process%"& Exit /B)
        Call :Delay %BugIntervalsDelay%
      ) else (
        Echo Waked up
        Exit /B
    )
  )
)
Exit /B
 
 
:Delay %1-msec.
ping 192.168.254.254 -n 2 -w %~1 > nul
Exit /B
 
:IsResponding %1-ProcessName
For /f "tokens=1,2 delims=," %%A in (
  'tasklist /FI "IMAGENAME eq %~1" /FI "STATUS eq NOT RESPONDING" /FO CSV /NH') do (
    if /i "%%~A"=="%~1" Exit /B %%~B
)
Exit /B 0
 
:Restart %1-Proccess_PID %2-Process_Name
Echo ShutDown
::Getting the Process' FullName (with Path)
for /f "tokens=2 delims==" %%A in (
  'wmic path win32_process where ^(ProcessID^=%~1 and name^="%~2"^) get "CommandLine" /value^|find "CommandLine"'
) do (Set ProcessFullName=%%A)
 
taskkill /F /PID %~1
Call :Delay %DelayStartProcess%
start "" %Minimize%/%Priority% %ProcessFullName%
Echo %ProcessFullName% started successfully.
Exit /B
 
:PriorityEnum %1-Constant %2-var.name
if %~1==1 Set %~2=LOW
if %~1==2 Set %~2=BELOWNORMAL
if %~1==3 Set %~2=NORMAL
if %~1==4 Set %~2=ABOVENORMAL
if %~1==5 Set %~2=HIGH
if %~1==6 Set %~2=REALTIME
Exit /B 
