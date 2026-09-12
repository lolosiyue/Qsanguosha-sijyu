Attribute VB_Name = "QsanBootstrap"
Option Explicit
#If VBA7 Then
Private Declare PtrSafe Function CoCreateGuid Lib "ole32" (ByRef guid As Any) As Long
Private Declare PtrSafe Function GetWindowThreadProcessId Lib "user32" (ByVal hwnd As LongPtr, ByRef pid As Long) As Long
#Else
Private Declare Function CoCreateGuid Lib "ole32" (ByRef guid As Any) As Long
Private Declare Function GetWindowThreadProcessId Lib "user32" (ByVal hwnd As Long, ByRef pid As Long) As Long
#End If
Private mPath As String, mNonce As String, mParentPid As Long, mPending As Boolean

Public Sub QsanBootstrapStart(Optional ByVal settings As Variant)
    Dim root As String, exePath As String, launcher As String, cmd As String, sh As Object
    Dim pid As Long
    mNonce = QsanGuid(): GetWindowThreadProcessId Application.hwnd, pid: mParentPid = pid
    EnsureFolder Environ$("TEMP") & "\QSanguoshaExcel"
    root = Environ$("TEMP") & "\QSanguoshaExcel\" & mNonce: EnsureFolder root
    mPath = root & "\bootstrap.json": mPending = True
    exePath = ThisWorkbook.Path & "\QSanguoshaExcelBridge.exe"
    launcher = ThisWorkbook.Path & "\tools\excel\LaunchExcel.vbs"
    If Len(Dir$(launcher)) = 0 Then launcher = ThisWorkbook.Path & "\LaunchExcel.vbs"
    QsanTransport.QsanRememberBootstrap mPath, exePath
    If Len(Dir$(launcher)) = 0 Or Len(Dir$(exePath)) = 0 Then Err.Raise vbObjectError + 750, , "private_launcher_missing"
    cmd = "wscript.exe //B //Nologo " & Quote(launcher) & " " & Quote(exePath) & " " & Quote(mPath) & " " & Quote(mNonce) & " " & CStr(pid) & " " & Quote(ThisWorkbook.Path)
    Set sh = CreateObject("WScript.Shell")
    ' LaunchExcel.vbs creates the private settings/data root before exec.
    sh.Run cmd, 0, False
End Sub

Public Sub QsanBootstrapPoll()
    Dim text As String, ready As Object
    If Not mPending Then Exit Sub
    If Len(Dir$(mPath)) = 0 Then Exit Sub
    text = ReadText(mPath)
    If Len(text) = 0 Then Exit Sub
    On Error GoTo invalid
    Set ready = QsanJsonParse(text)
    If IsErrorReady(ready) Then
        mPending = False
        QsanTransport.QsanBootstrapFailed CStr(ready("error"))
        Exit Sub
    End If
    If ReadyMatches(ready) Then mPending = False: QsanTransport.QsanHandleBootstrap ready
    Exit Sub
invalid:
    ' Atomic publication may be observed between rename and close; retry next tick.
End Sub

Public Sub QsanBootstrapCancel()
    Dim sh As Object, cmd As String
    QsanTransport.QsanMarkBootstrapCancelled
    mPending = False
    ' Keep the path: a hidden stop helper can claim a ready bridge published later.
    On Error Resume Next
    If Len(Dir$(ThisWorkbook.Path & "\QSanguoshaExcelBridge.exe")) > 0 Then
        Set sh = CreateObject("WScript.Shell")
        cmd = Quote(ThisWorkbook.Path & "\QSanguoshaExcelBridge.exe") & " --stop-session " & Quote(mPath) & " --wait-ready 180000 --parent-pid " & CStr(mParentPid)
        sh.Run cmd, 0, False
    End If
    On Error GoTo 0
End Sub
Private Function ReadyMatches(ByVal ready As Object) As Boolean
    On Error GoTo invalid
    If CLng(ready("api_version")) <> 1 Then Exit Function
    If CStr(ready("nonce")) <> mNonce Or CStr(ready("status")) <> "ready" Then Exit Function
    If CLng(ready("parent_pid")) <> mParentPid Then Exit Function
    If CLng(ready("port")) <= 0 Or CLng(ready("port")) > 65535 Or CLng(ready("pid")) <= 0 Then Exit Function
    If Not IsGuid(CStr(ready("session"))) Or Len(CStr(ready("token"))) <> 64 Then Exit Function
    ReadyMatches = True
invalid:
End Function
Private Function IsGuid(ByVal value As String) As Boolean
    Dim i As Long, ch As String
    If Len(value) <> 36 Then Exit Function
    If Mid$(value, 9, 1) <> "-" Or Mid$(value, 14, 1) <> "-" Or Mid$(value, 19, 1) <> "-" Or Mid$(value, 24, 1) <> "-" Then Exit Function
    For i = 1 To Len(value)
        ch = Mid$(value, i, 1)
        If ch <> "-" And InStr(1, "0123456789abcdefABCDEF", ch, vbBinaryCompare) = 0 Then Exit Function
    Next i
    IsGuid = True
End Function
Private Function IsErrorReady(ByVal ready As Object) As Boolean
    On Error GoTo done
    IsErrorReady = (CStr(ready("nonce")) = mNonce And CStr(ready("status")) = "error")
done:
End Function
Private Function ReadText(ByVal path As String) As String
    Dim f As Integer: On Error GoTo done
    f = FreeFile: Open path For Binary Access Read Lock Read As #f
    If LOF(f) > 1048576 Then Close #f: Exit Function
    ReadText = Space$(LOF(f)): Get #f, , ReadText: Close #f
done:
    On Error Resume Next: If f <> 0 Then Close #f: On Error GoTo 0
End Function
Private Sub EnsureFolder(ByVal path As String)
    Dim fso As Object: Set fso = CreateObject("Scripting.FileSystemObject")
    If Not fso.FolderExists(path) Then fso.CreateFolder path
End Sub
Private Function Quote(ByVal value As String) As String: Quote = Chr$(34) & Replace$(value, Chr$(34), Chr$(34) & Chr$(34)) & Chr$(34): End Function
Private Function QsanGuid() As String
    Dim b(0 To 15) As Byte, i As Long, h As String
    If CoCreateGuid(b(0)) <> 0 Then Err.Raise vbObjectError + 751, , "guid_failed"
    For i = 0 To 15: h = h & Right$("0" & Hex$(b(i)), 2): Next
    QsanGuid = Mid$(h, 1, 8) & "-" & Mid$(h, 9, 4) & "-" & Mid$(h, 13, 4) & "-" & Mid$(h, 17, 4) & "-" & Mid$(h, 21)
End Function
Public Sub QsanBootstrapStop(ByVal path As String, Optional ByVal bridgePath As String = "", Optional ByVal waitReadyMs As Long = 0, Optional ByVal parentPid As Long = 0)
    Dim sh As Object, cmd As String
    On Error Resume Next
    If Len(bridgePath) > 0 And Len(Dir$(bridgePath)) > 0 Then
        Set sh = CreateObject("WScript.Shell")
        cmd = Quote(bridgePath) & " --stop-session " & Quote(path)
        If waitReadyMs > 0 Then cmd = cmd & " --wait-ready " & CStr(waitReadyMs)
        If parentPid > 0 Then cmd = cmd & " --parent-pid " & CStr(parentPid)
        sh.Run cmd, 0, False
    End If
    On Error GoTo 0
End Sub
