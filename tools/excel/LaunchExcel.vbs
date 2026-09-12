Option Explicit

' Isolate native settings/user data in the bridge child process tree.
Dim sh, env, fso, exePath, bootstrapPath, nonce, parentPid, assetRoot, sessionDir
If WScript.Arguments.Count < 5 Then WScript.Quit 2
exePath = WScript.Arguments(0)
bootstrapPath = WScript.Arguments(1)
nonce = WScript.Arguments(2)
parentPid = WScript.Arguments(3)
assetRoot = WScript.Arguments(4)
Set fso = CreateObject("Scripting.FileSystemObject")
If Not fso.FileExists(exePath) Then WScript.Quit 3
If Not fso.FileExists(WScript.ScriptFullName) Then WScript.Quit 4
sessionDir = fso.GetParentFolderName(bootstrapPath)
If Not fso.FolderExists(fso.GetParentFolderName(sessionDir)) Then fso.CreateFolder fso.GetParentFolderName(sessionDir)
If Not fso.FolderExists(sessionDir) Then fso.CreateFolder sessionDir
If Not fso.FolderExists(sessionDir & "\data") Then fso.CreateFolder sessionDir & "\data"
If Not fso.FileExists(sessionDir & "\config.ini") Then
  Dim cfg: Set cfg = fso.CreateTextFile(sessionDir & "\config.ini", True, False): cfg.Close
End If
Set sh = CreateObject("WScript.Shell")
Set env = sh.Environment("Process")
env("QSAN_SESSION_SETTINGS") = sessionDir & "\config.ini"
env("QSAN_USER_DATA_ROOT") = sessionDir & "\data"
env("QSAN_PRIVATE_ENV_READY") = "1"
sh.CurrentDirectory = sessionDir
sh.Run """" & exePath & """ --bootstrap """ & bootstrapPath & """ --nonce " & nonce & " --parent-pid " & parentPid & " --asset-root """ & assetRoot & """", 0, False
