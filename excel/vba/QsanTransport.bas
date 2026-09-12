Attribute VB_Name = "QsanTransport"
Option Explicit

Private Const MAX_QUEUE As Long = 128
Private Const DEADLINE As Long = 15
Private mSession As String, mToken As String, mHost As String, mBootstrap As String, mBridgePath As String
Private mGeneration As String, mRevision As String, mAfter As String, mNextId As String
Private mRunning As Boolean, mStarting As Boolean, mCancelled As Boolean, mInTick As Boolean
Private mScheduled As Boolean, mTickAt As Date, mLastTick As Date, mSnapshotAt As Date, mStartAt As Date
Private mFreshRequired As Boolean
Private mUpdates As Object, mCommand As Object, mActive As Object, mQueue As Collection
Private mUpdatesAt As Date, mCommandAt As Date, mBody As String, mId As String, mRetries As Long

Public Sub QsanStartLocal(Optional ByVal settings As Variant)
    If mRunning Or mStarting Then Exit Sub
    mCancelled = False: mStarting = True: mStartAt = Now
    mGeneration = "0": mRevision = "0": mAfter = "0": mNextId = "0"
    Set mQueue = New Collection
    On Error GoTo failed
    QsanBootstrapStart settings
    ScheduleNextTick
    Exit Sub
failed:
    QsanBootstrapFailed "bootstrap_launch_failed"
End Sub

Public Sub QsanQueueCommand(ByVal name As String, Optional ByVal args As Variant, Optional ByVal preflight As Boolean = False)
    Dim item As Object, copied As Object, i As Long
    On Error GoTo invalid
    If Not mRunning And Not mStarting Then
        QsanReportError "not_running"
        Exit Sub
    End If
    If IsMissing(args) Then
        Set copied = QsanDict()
    ElseIf IsObject(args) Then
        ' Freeze the draft at click time, not when a later tick transmits it.
        Set copied = QsanJsonParse(QsanJsonStringify(args))
    Else
        GoTo invalid
    End If
    If IsInteraction(name) Then
        If Not SnapshotFresh() Then
            NeedFreshSnapshot
            QsanReportError "stale_snapshot"
            ScheduleNextTick
            Exit Sub
        End If
    End If
    If mQueue Is Nothing Then Set mQueue = New Collection
    If preflight And name = "select" Then
        For i = mQueue.Count To 1 Step -1
            If CStr(mQueue(i)("name")) = "select" Then mQueue.Remove i
        Next i
    End If
    If mQueue.Count >= MAX_QUEUE Then
        QsanReportError "command_queue_full"
        Exit Sub
    End If
    Set item = QsanDict()
    item.Add "name", name: item.Add "args", copied
    item.Add "generation", mGeneration: item.Add "revision", mRevision
    mQueue.Add item
    ScheduleNextTick
    Exit Sub
invalid:
    QsanReportError "invalid_command"
End Sub

Private Function IsInteraction(ByVal name As String) As Boolean
    IsInteraction = (name = "select" Or name = "submit" Or name = "cancel")
End Function
Private Function SnapshotFresh() As Boolean
    If Not mRunning Or mFreshRequired Or mSnapshotAt = 0 Then Exit Function
    SnapshotFresh = (DateDiff("s", mSnapshotAt, Now) >= 0 And DateDiff("s", mSnapshotAt, Now) <= 3)
End Function

Private Sub NeedFreshSnapshot()
    mFreshRequired = True
    AbortRequest mUpdates
End Sub

Public Sub QsanTick()
    If mInTick Then Exit Sub
    mScheduled = False
    If Not mRunning And Not mStarting Then Exit Sub
    mInTick = True
    On Error GoTo failed
    If mLastTick <> 0 Then
        If DateDiff("s", mLastTick, Now) > 3 Then NeedFreshSnapshot
    End If
    mLastTick = Now
    If mStarting Then
        QsanBootstrapPoll
        If mStarting Then
            If DateDiff("s", mStartAt, Now) >= 180 Then QsanBootstrapFailed "bootstrap_deadline"
        End If
    End If
    If mRunning Then
        HarvestUpdates
        HarvestCommand
        If Not mRunning Then GoTo done
        If mUpdates Is Nothing Then
            Set mUpdates = CreateRequest("GET", "/v1/updates?after=" & mAfter, "", mUpdatesAt)
        End If
        If mCommand Is Nothing Then SendNextCommand
    End If
done:
    mInTick = False
    ScheduleNextTick
    Exit Sub
failed:
    QsanReportError "tick_failed"
    Resume done
End Sub

Public Sub ScheduleNextTick()
    If mScheduled Then Exit Sub
    If Not mRunning And Not mStarting Then Exit Sub
    On Error GoTo failed
    mTickAt = DateAdd("s", 1, Now)
    Application.OnTime mTickAt, TickMacro(), , True
    mScheduled = True
failed:
End Sub

Private Function TickMacro() As String
    TickMacro = "'" & Replace$(ThisWorkbook.Name, "'", "''") & "'!QsanTick"
End Function

Private Function CreateRequest(ByVal method As String, ByVal route As String, ByVal body As String, ByRef started As Date) As Object
    Dim request As Object
    Set request = CreateObject("WinHttp.WinHttpRequest.5.1")
    request.Open method, mHost & route, True
    request.SetProxy 1
    request.Option(6) = False
    request.SetTimeouts 1000, 1000, 5000, 15000
    request.SetRequestHeader "Authorization", "Bearer " & mToken
    request.SetRequestHeader "X-QSan-Session", mSession
    If method = "POST" Then request.SetRequestHeader "Content-Type", "application/json; charset=utf-8"
    started = Now
    If method = "GET" Then
        request.Send
    Else
        request.Send body
    End If
    Set CreateRequest = request
End Function

Private Sub SendNextCommand()
    Dim body As Object, args As Object, name As String
    On Error GoTo failed
    If mActive Is Nothing Then
        If mQueue Is Nothing Then Exit Sub
        If mQueue.Count = 0 Then Exit Sub
        Set mActive = mQueue(1): mQueue.Remove 1
        name = CStr(mActive("name")): Set args = mActive("args")
        If IsInteraction(name) Then
            If Not SnapshotFresh() Then
                DropCommand
                QsanReportError "stale_snapshot"
                Exit Sub
            End If
            If CStr(args("generation")) <> mGeneration Or CStr(args("revision")) <> mRevision Then
                DropCommand
                QsanReportError "stale_interaction"
                Exit Sub
            End If
        End If
        Set body = QsanDict()
        mId = NextDecimalId()
        body.Add "api_version", 1: body.Add "session", mSession: body.Add "id", mId
        body.Add "generation", CStr(mActive("generation")): body.Add "revision", CStr(mActive("revision"))
        body.Add "name", name: body.Add "args", args
        mBody = QsanJsonStringify(body): mRetries = 0
    End If
    Set mCommand = CreateRequest("POST", "/v1/commands", mBody, mCommandAt)
    Exit Sub
failed:
    RetryCommand "command_start_failed"
End Sub

Private Function ReadReply(ByVal request As Object) As Object
    Dim data As Object, text As String, version As Variant
    If request.Status <> 200 Then Err.Raise vbObjectError + 811
    text = request.ResponseText
    If Len(text) > 4194304 Then Err.Raise vbObjectError + 812
    Set data = QsanJsonParse(text)
    version = data("api_version")
    If VarType(version) = vbString Or IsObject(version) Or IsNull(version) Then Err.Raise vbObjectError + 813
    If CDbl(version) <> 1 Then Err.Raise vbObjectError + 813
    If CStr(data("session")) <> mSession Then Err.Raise vbObjectError + 814
    Set ReadReply = data
End Function

Private Sub HarvestUpdates()
    Dim data As Object, snapshot As Object, seq As String
    If mUpdates Is Nothing Then Exit Sub
    On Error GoTo failed
    If Not mUpdates.WaitForResponse(0) Then
        If DateDiff("s", mUpdatesAt, Now) >= DEADLINE Then
            AbortRequest mUpdates
            mFreshRequired = True
        End If
        Exit Sub
    End If
    Set data = ReadReply(mUpdates)
    RequireDecimal data("sequence")
    seq = CStr(data("sequence"))
    If DecimalLess(seq, mAfter) Then Err.Raise vbObjectError + 815
    Set snapshot = data("snapshot")
    RequireDecimal snapshot("generation"): RequireDecimal snapshot("revision"): RequireDecimal snapshot("request_id")
    mGeneration = CStr(snapshot("generation")): mRevision = CStr(snapshot("revision")): mAfter = seq
    Set mUpdates = Nothing
    mSnapshotAt = Now: mFreshRequired = False
    QsanUi.ApplySnapshot snapshot
    Exit Sub
failed:
    AbortRequest mUpdates
    mFreshRequired = True
    QsanReportError "invalid_updates_response"
End Sub

Private Sub HarvestCommand()
    Dim data As Object, name As String, args As Object, wasDisconnect As Boolean
    If mCommand Is Nothing Then Exit Sub
    On Error GoTo failed
    If Not mCommand.WaitForResponse(0) Then
        If DateDiff("s", mCommandAt, Now) >= DEADLINE Then RetryCommand "command_deadline"
        Exit Sub
    End If
    Set data = ReadReply(mCommand)
    RequireDecimal data("id")
    If CStr(data("id")) <> mId Then Err.Raise vbObjectError + 816
    If VarType(data("ok")) <> vbBoolean Then Err.Raise vbObjectError + 817
    name = CStr(mActive("name")): Set args = mActive("args")
    If data.Exists("error") Then
        If CStr(data("error")) = "stale_interaction" Then NeedFreshSnapshot
    End If
    wasDisconnect = (name = "disconnect" And CBool(data("ok")))
    ' A UI rendering error must not retry an already correlated command.
    DropCommand
    On Error GoTo uiFailed
    QsanInteraction_Result name, args, data
    QsanUi_CommandResult name, data
    QsanHandleCommandResult data
    If wasDisconnect Then QsanStop
    Exit Sub
uiFailed:
    QsanReportError "command_display_failed"
    Exit Sub
failed:
    RetryCommand "command_response_failed"
End Sub

Private Sub RetryCommand(ByVal reason As String)
    AbortRequest mCommand
    If mActive Is Nothing Then Exit Sub
    mRetries = mRetries + 1
    If mRetries > 3 Or Len(mBody) = 0 Then
        DropCommand
        NeedFreshSnapshot
        QsanReportError reason
    End If
End Sub

Private Sub AbortRequest(ByRef request As Object)
    On Error Resume Next
    If Not request Is Nothing Then request.Abort
    Set request = Nothing
    On Error GoTo 0
End Sub

Private Sub DropCommand()
    AbortRequest mCommand
    Set mActive = Nothing
    mBody = "": mId = "": mRetries = 0
End Sub

Public Sub QsanStop()
    On Error Resume Next
    If mScheduled Then Application.OnTime mTickAt, TickMacro(), , False
    mScheduled = False
    AbortRequest mUpdates: DropCommand
    If mStarting Then
        QsanBootstrapCancel
    ElseIf mRunning Then
        ' Recovery owns its own event loop and can finish after workbook close.
        QsanBootstrapStop mBootstrap, mBridgePath, 180000
    End If
    mRunning = False: mStarting = False: mCancelled = True
    mSession = "": mToken = "": mHost = "": mGeneration = "": mRevision = "": mAfter = ""
    mBootstrap = "": mBridgePath = "": mSnapshotAt = 0: mLastTick = 0: mFreshRequired = True
    Set mQueue = Nothing
    On Error GoTo 0
End Sub

Public Sub QsanBootstrapFailed(ByVal reason As String)
    QsanStop
    QsanReportError reason
End Sub
Public Sub QsanMarkBootstrapCancelled()
    mCancelled = True
End Sub
Public Sub QsanRememberBootstrap(ByVal path As String, Optional ByVal bridgePath As String = "")
    mBootstrap = path: mBridgePath = bridgePath
End Sub
Public Sub QsanHandleBootstrap(ByVal ready As Object)
    If mCancelled Then
        QsanBootstrapStop mBootstrap, mBridgePath, 180000
        Exit Sub
    End If
    mSession = CStr(ready("session")): mToken = CStr(ready("token"))
    mHost = "http://127.0.0.1:" & CStr(ready("port"))
    mRunning = True: mStarting = False: mFreshRequired = True
    If mQueue Is Nothing Then Set mQueue = New Collection
End Sub
Public Property Get QsanSession() As String
    QsanSession = mSession
End Property
Public Property Get QsanIsRunning() As Boolean
    QsanIsRunning = mRunning Or mStarting
End Property
Public Property Get QsanGeneration() As String
    QsanGeneration = mGeneration
End Property
Public Property Get QsanRevision() As String
    QsanRevision = mRevision
End Property
Public Sub QsanConnect(ByVal host As String, ByVal port As Long, ByVal playerName As String, ByVal avatar As String)
    Dim d As Object: Set d = QsanDict()
    d.Add "host", host: d.Add "port", port: d.Add "name", playerName: d.Add "avatar", avatar
    QsanQueueCommand "connect", d
End Sub
Public Sub QsanHandleCommandResult(ByVal result As Object)
    QsanUi.ShowStatus result
End Sub
Public Sub QsanReportError(ByVal code As String)
    Dim d As Object
    On Error Resume Next
    Set d = QsanDict(): d.Add "ok", False: d.Add "error", code
    QsanUi.ShowStatus d
    On Error GoTo 0
End Sub
Public Function QsanDict() As Object
    Set QsanDict = CreateObject("Scripting.Dictionary")
End Function

Private Sub RequireDecimal(ByVal value As Variant)
    Dim s As String, i As Long, c As String
    If VarType(value) <> vbString Then Err.Raise vbObjectError + 818
    s = CStr(value)
    If Len(s) = 0 Or Len(s) > 20 Then Err.Raise vbObjectError + 818
    If Len(s) > 1 And Left$(s, 1) = "0" Then Err.Raise vbObjectError + 818
    For i = 1 To Len(s)
        c = Mid$(s, i, 1)
        If c < "0" Or c > "9" Then Err.Raise vbObjectError + 818
    Next i
    If Len(s) = 20 Then
        If StrComp(s, "18446744073709551615", vbBinaryCompare) > 0 Then Err.Raise vbObjectError + 818
    End If
End Sub
Private Function DecimalLess(ByVal a As String, ByVal b As String) As Boolean
    If Len(a) <> Len(b) Then
        DecimalLess = Len(a) < Len(b)
    Else
        DecimalLess = StrComp(a, b, vbBinaryCompare) < 0
    End If
End Function
Private Function NextDecimalId() As String
    Dim i As Long, n As Long, carry As Long, result As String
    If Len(mNextId) = 0 Then mNextId = "0"
    carry = 1
    For i = Len(mNextId) To 1 Step -1
        n = Asc(Mid$(mNextId, i, 1)) - 48 + carry
        If n >= 10 Then
            n = n - 10
        Else
            carry = 0
        End If
        result = CStr(n) & result
    Next i
    If carry = 1 Then result = "1" & result
    RequireDecimal result
    mNextId = result: NextDecimalId = result
End Function
