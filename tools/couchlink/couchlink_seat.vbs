' Starts couchlink_seat.ps1 without any visible window (window style 0) and returns at once.
' Usage: wscript.exe //B //Nologo couchlink_seat.vbs <seat>
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
dir = fso.GetParentFolderName(WScript.ScriptFullName)
seat = "0"
If WScript.Arguments.Count > 0 Then seat = WScript.Arguments(0)
sh.Run "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & dir & "\couchlink_seat.ps1"" -Seat " & seat, 0, False
