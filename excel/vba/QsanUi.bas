Attribute VB_Name = "QsanUi"
Option Explicit

Private Const PAGE_SIZE As Long = 8
Private mBanks As Object, mPages As Object, mRows As Object, mBankHashes As Object
Private mLastSnapshot As Object

Public Sub QsanUi_Bootstrap()
    Dim names, name As Variant, ws As Worksheet, existing As Shape
    Set mBanks = QsanDict(): Set mPages = QsanDict(): Set mRows = QsanDict(): Set mBankHashes = QsanDict()
    names = Array("首頁", "房間設定", "牌桌", "詳情", "戰報")
    For Each name In names
        Set ws = GetSheet(CStr(name))
        For Each existing In ws.Shapes
            If Left$(existing.Name, 5) = "QSan_" Or Left$(existing.Name, 7) = "QSanUI_" Then existing.Visible = msoFalse
        Next existing
        ws.Range("A1:H36").ClearContents
        ws.Activate: ActiveWindow.Zoom = 80
        ws.Columns("A:H").ColumnWidth = 13
        ws.Rows("1:36").RowHeight = 18
        ws.Range("A1:H36").NumberFormat = "@"
        QsanUi_Text ws.Range("A1"), CStr(name)
    Next name
    Set ws = GetSheet("首頁")
    QsanUi_Text ws.Range("A3"), "伺服器": QsanUi_Text ws.Range("B3"), "127.0.0.1"
    QsanUi_Text ws.Range("A4"), "連接埠": ws.Range("B4").Value2 = 9527
    QsanUi_Text ws.Range("A5"), "名稱": QsanUi_Text ws.Range("B5"), "Excel"
    QsanUi_Text ws.Range("A6"), "頭像": QsanUi_Text ws.Range("B6"), "caocao"
    QsanUi_Text ws.Range("A7"), "私人房間": QsanUi_Text ws.Range("B7"), "true"
    QsanUi_Text ws.Range("A8"), "電腦數": ws.Range("B8").Value2 = 0
    QsanUi_Button "首頁", "連線", "QsanUi_Connect", "D3"
    QsanUi_Button "首頁", "建立房間", "QsanUi_Host", "F3"
    QsanUi_Button "首頁", "本機 AI", "QsanUi_LocalAI", "D5"
    QsanUi_Button "首頁", "取得目錄", "QsanUi_Catalog", "F5"
    QsanUi_Button "首頁", "房間設定", "QsanUi_SettingsPage", "D7"
    QsanUi_Button "首頁", "重新連線", "QsanUi_Reconnect", "F7"
    Set ws = GetSheet("牌桌")
    QsanUi_Button "牌桌", "確認", "QsanUi_Confirm", "A4"
    QsanUi_Button "牌桌", "取消", "QsanUi_Cancel", "B4"
    QsanUi_Button "牌桌", "預檢", "QsanInteraction_Preview", "C4"
    QsanUi_Button "牌桌", "清空選擇", "QsanInteraction_Clear", "D4"
    QsanUi_Button "牌桌", "撤回目標", "QsanInteraction_UndoTarget", "E4"
    QsanUi_Button "牌桌", "放牌頂", "QsanInteraction_Top", "F4"
    QsanUi_Button "牌桌", "放牌底", "QsanInteraction_Bottom", "G4"
    QsanUi_Button "牌桌", "結束出牌", "QsanUi_EndTurn", "H4"
    QsanUi_Text ws.Range("A6"), "依序點選；觀星先選牌頂/牌底；分配先選牌再選玩家"
    QsanUi_Button "牌桌", "準備", "QsanUi_Ready", "A36"
    QsanUi_Button "牌桌", "取消準備", "QsanUi_Unready", "B36"
    QsanUi_Button "牌桌", "託管", "QsanUi_Trust", "C36"
    QsanUi_Button "牌桌", "取消託管", "QsanUi_Untrust", "D36"
    QsanUi_Button "牌桌", "投降", "QsanUi_Surrender", "E36"
    QsanUi_Button "牌桌", "加電腦", "QsanUi_AddRobot", "F36"
    QsanUi_Button "牌桌", "回首頁", "QsanUi_Disconnect", "G36"
    Set ws = GetSheet("戰報")
    QsanUi_Text ws.Range("A3"), "聊天文字"
    QsanUi_Button "戰報", "送出", "QsanUi_Chat", "G3"
    QsanInteraction_Initialize
    QsanUi_Home
    ActiveWindow.Zoom = 80
End Sub
Public Sub QsanUi_Home(): GetSheet("首頁").Activate: End Sub
Public Sub QsanUi_SettingsPage(): GetSheet("房間設定").Activate: End Sub
Public Sub QsanUi_Connect()
    EnsureStarted
    Dim d As Object: Set d = IdentityFields()
    d.Add "host", CStr(GetSheet("首頁").Range("B3").Value2)
    d.Add "port", CLng(Val(GetSheet("首頁").Range("B4").Value2))
    QsanQueueCommand "connect", d
    GetSheet("牌桌").Activate
End Sub
Public Sub QsanUi_Host()
    EnsureStarted
    Dim d As Object: Set d = IdentityFields()
    d.Add "private", LCase$(CStr(GetSheet("首頁").Range("B7").Value2)) = "true"
    d.Add "robots", CLng(Val(GetSheet("首頁").Range("B8").Value2))
    d.Add "settings", QsanCatalog_Settings()
    QsanQueueCommand "host", d
    GetSheet("牌桌").Activate
End Sub
Public Sub QsanUi_LocalAI()
    QsanUi_Text GetSheet("首頁").Range("B7"), "true"
    QsanUi_Text GetSheet("首頁").Range("B8"), "0"
    ' robots=0 is the bridge contract for filling remaining seats.
    QsanUi_Host
End Sub
Public Sub QsanUi_Catalog(): EnsureStarted: QsanQueueCommand "catalog", QsanDict(): End Sub
Public Sub QsanUi_Ready(): SendFlag "ready", "ready", True: End Sub
Public Sub QsanUi_Unready(): SendFlag "ready", "ready", False: End Sub
Public Sub QsanUi_Trust(): SendFlag "trust", "enabled", True: End Sub
Public Sub QsanUi_Untrust(): SendFlag "trust", "enabled", False: End Sub
Public Sub QsanUi_Surrender(): QsanQueueCommand "surrender", QsanDict(): End Sub
Public Sub QsanUi_Reconnect(): EnsureStarted: QsanQueueCommand "reconnect", QsanDict(): End Sub
Public Sub QsanUi_Disconnect(): QsanQueueCommand "disconnect", QsanDict(): QsanUi_Home: End Sub
Public Sub QsanUi_AddRobot()
    Dim d As Object: Set d = QsanDict(): d.Add "count", 1: QsanQueueCommand "add_robot", d
End Sub
Public Sub QsanUi_Chat()
    Dim d As Object: Set d = QsanDict()
    d.Add "text", CStr(GetSheet("戰報").Range("B3").Value2)
    QsanQueueCommand "chat", d
End Sub
Public Sub QsanUi_Cancel(): QsanInteraction_Cancel: End Sub
Public Sub QsanUi_Confirm(): QsanInteraction_Confirm: End Sub
Public Sub QsanUi_EndTurn(): QsanInteraction_EndTurn: End Sub
Public Sub ApplySnapshot(ByVal snapshot As Object): QsanUi_ApplySnapshot snapshot: End Sub
Public Sub QsanUi_ApplySnapshot(ByVal snapshot As Object)
    If snapshot Is Nothing Then Exit Sub
    Set mLastSnapshot = snapshot
    QsanUi_RenderView QsanObject(snapshot, "view")
    Dim request As Object: Set request = QsanObject(snapshot, "interaction")
    If request Is Nothing Then Set request = QsanDict()
    ' Counters belong to the snapshot even when canonical request omits them.
    request("generation") = QsanText(snapshot, "generation")
    request("revision") = QsanText(snapshot, "revision")
    QsanInteraction_Apply request, GetSheet("牌桌")
End Sub
Public Sub ShowStatus(ByVal result As Variant)
    If Not IsObject(result) Then Exit Sub
    Dim r As Object: Set r = QsanObject(result, "result")
    If Len(QsanText(result, "error")) > 0 Then QsanUi_Status QsanText(result, "error")
    If r Is Nothing Then Exit Sub
    If Not QsanObject(r, "modes") Is Nothing Then
        QsanCatalog_Apply r: QsanCatalog_RenderSettings GetSheet("房間設定")
    End If
    If Not QsanObject(r, "details") Is Nothing Then RenderDetails QsanObject(r, "details")
End Sub
Public Sub QsanUi_CommandResult(ByVal commandName As String, ByVal envelope As Object)
    If commandName = "details" Then
        If Not QsanObject(envelope, "result") Is Nothing Then RenderDetails QsanObject(envelope, "result")
    End If
End Sub
Public Sub QsanUi_InteractionButtons(ByVal canConfirm As Boolean, ByVal canCancel As Boolean)
    Dim s As Shape
    Set s = PoolShape(GetSheet("牌桌"), "BTN_QsanUi_Confirm")
    s.Fill.ForeColor.RGB = IIf(canConfirm, RGB(100, 190, 130), RGB(180, 180, 180))
    Set s = PoolShape(GetSheet("牌桌"), "BTN_QsanUi_Cancel")
    s.Fill.ForeColor.RGB = IIf(canCancel, RGB(230, 239, 247), RGB(180, 180, 180))
End Sub
Public Sub QsanUi_Status(ByVal value As String)
    QsanUi_Text GetSheet("牌桌").Range("A5"), value
    QsanUi_Text GetSheet("首頁").Range("A10"), value
End Sub
Public Sub QsanUi_RenderView(ByVal view As Object)
    If view Is Nothing Then Exit Sub
    QsanUi_Bank "hand", QsanObject(view, "hand"), "手牌", "card", 1, 20
    QsanUi_Bank "players", QsanObject(view, "players"), "玩家", "target", 3, 20
    QsanUi_Bank "ownedskills", QsanObject(view, "skills"), "已有技能", "skill", 5, 20
    QsanUi_Bank "publiccards", QsanObject(view, "cards"), "裝備 / 公開牌", "card", 7, 20, "詳情"
    QsanUi_Bank "logs", QsanObject(view, "logs"), "戰報", "log", 1, 7, "戰報"
    QsanUi_Text GetSheet("牌桌").Range("H2"), QsanText(view, "status")
    If LCase$(QsanText(view, "game_over")) = "true" Then QsanUi_Status "對局已結束"
End Sub

' Each independent bank owns eight stable row and image shapes, plus paging.
' Updating one bank never clears another bank or a user's selected collection.
Public Sub QsanUi_Bank(ByVal bank As String, ByVal rows As Object, ByVal title As String, ByVal action As String, ByVal col As Long, ByVal firstRow As Long, Optional ByVal sheetName As String = "牌桌")
    If mBanks Is Nothing Then Exit Sub
    Dim descriptor As Object: Set descriptor = QsanDict()
    descriptor.Add "rows", rows: descriptor.Add "title", title: descriptor.Add "action", action
    descriptor.Add "col", col: descriptor.Add "row", firstRow: descriptor.Add "sheet", sheetName
    If mBanks.Exists(bank) Then mBanks.Remove bank
    mBanks.Add bank, descriptor
    RenderBank bank
End Sub
Private Sub RenderBank(ByVal bank As String)
    Dim d As Object, rows As Object, ws As Worksheet, page As Long, i As Long, n As Long
    Dim item As Object, key As String, label As String, detail As String, enabled As Boolean
    Dim s As Shape, pic As Shape, col As Long, r As Long, token As String, meta As Object
    Set d = mBanks(bank): Set rows = QsanObject(d, "rows"): Set ws = GetSheet(CStr(d("sheet")))
    col = CLng(d("col")): r = CLng(d("row"))
    If mPages.Exists(bank) Then page = CLng(mPages(bank))
    If Not rows Is Nothing Then n = rows.Count
    If page * PAGE_SIZE >= n Then page = 0
    mPages(bank) = page
    Dim signature As String: signature = CStr(page) & "|none"
    If Not rows Is Nothing Then signature = CStr(page) & "|" & QsanJsonStringify(rows)
    If mBankHashes.Exists(bank) Then
        If CStr(mBankHashes(bank)) = signature Then Exit Sub
    End If
    mBankHashes(bank) = signature
    QsanUi_Text ws.Cells(r - 1, col), CStr(d("title")) & " " & CStr(page + 1) & "/" & CStr((n + PAGE_SIZE - 1) \ PAGE_SIZE)
    BankPager ws, bank, col, r + PAGE_SIZE, -1
    BankPager ws, bank, col + 1, r + PAGE_SIZE, 1
    For i = 1 To PAGE_SIZE
        token = bank & "_" & CStr(i)
        Set s = PoolShape(ws, "ROW_" & token)
        Set pic = PoolShape(ws, "PIC_" & token)
        If page * PAGE_SIZE + i > n Then
            s.Visible = msoFalse: pic.Visible = msoFalse
            If mRows.Exists(token) Then mRows.Remove token
        Else
            If IsObject(rows(page * PAGE_SIZE + i)) Then
                Set item = rows(page * PAGE_SIZE + i)
                key = QsanText(item, "id")
                If Len(key) = 0 Then key = QsanText(item, "value")
                If Len(key) = 0 Then key = QsanText(item, "response_value")
                If Len(key) = 0 Then key = QsanText(item, "name")
                label = QsanText(item, "label")
                If Len(label) = 0 Then label = QsanText(item, "skill")
                If Len(label) = 0 Then label = key
                enabled = (LCase$(QsanText(item, "enabled")) <> "false")
                detail = QsanText(item, "detail")
                If Len(QsanText(item, "hp")) > 0 Then label = label & " " & QsanText(item, "hp") & "/" & QsanText(item, "max_hp") & " 手牌" & QsanText(item, "hand_count")
            Else
                Set item = Nothing: key = CStr(rows(page * PAGE_SIZE + i)): label = key: enabled = True: detail = key
            End If
            Set meta = QsanDict(): meta.Add "key", key: meta.Add "action", d("action")
            meta.Add "enabled", enabled: meta.Add "detail", detail: meta.Add "instance", 0
            If Not item Is Nothing Then
                meta("instance") = CLng(Val(QsanText(item, "instance_id")))
                If CLng(meta("instance")) = 0 Then meta("instance") = CLng(Val(QsanText(item, "skill_instance_id")))
                If CStr(d("action")) = "skill" Then
                    If Len(QsanText(item, "name")) > 0 Then meta("key") = QsanText(item, "name")
                    If Len(QsanText(item, "skill")) > 0 Then meta("key") = QsanText(item, "skill")
                End If
            End If
            If mRows.Exists(token) Then mRows.Remove token
            mRows.Add token, meta
            s.Left = ws.Cells(r + i - 1, col).Left: s.Top = ws.Cells(r + i - 1, col).Top
            s.Width = ws.Range(ws.Cells(1, col), ws.Cells(1, col + 1)).Width - 2: s.Height = 17
            If s.TextFrame.Characters.Text <> label Then s.TextFrame.Characters.Text = label
            s.AlternativeText = token: s.OnAction = MacroName("QsanUi_RowClick")
            s.Fill.ForeColor.RGB = IIf(enabled, RGB(230, 239, 247), RGB(180, 180, 180))
            s.Visible = msoTrue
            pic.Visible = msoFalse
            If Not item Is Nothing Then
                Dim path As String: path = QsanText(item, "image")
                If Len(path) > 0 Then
                    On Error Resume Next
                    If pic.AlternativeText <> path Then pic.Fill.UserPicture path
                    If Err.Number = 0 Then
                        pic.AlternativeText = path: pic.Left = s.Left + s.Width - 17: pic.Top = s.Top
                        pic.Width = 17: pic.Height = 17: pic.Visible = msoTrue
                        pic.OnAction = s.OnAction
                        ' Image click resolves to the same row through its shape name.
                    End If
                    Err.Clear: On Error GoTo 0
                End If
            End If
        End If
        Set item = Nothing
    Next i
End Sub
Private Sub BankPager(ByVal ws As Worksheet, ByVal bank As String, ByVal col As Long, ByVal row As Long, ByVal delta As Long)
    Dim s As Shape: Set s = PoolShape(ws, "PAGE_" & bank & CStr(delta))
    s.Left = ws.Cells(row, col).Left: s.Top = ws.Cells(row, col).Top: s.Width = ws.Columns(col).Width - 2: s.Height = 17
    s.TextFrame.Characters.Text = IIf(delta < 0, "上一頁", "下一頁")
    s.AlternativeText = bank & "|" & CStr(delta): s.OnAction = MacroName("QsanUi_PageClick"): s.Visible = msoTrue
End Sub
Public Sub QsanUi_PageClick()
    Dim parts, bank As String, page As Long
    parts = Split(ActiveSheet.Shapes(Application.Caller).AlternativeText, "|"): bank = CStr(parts(0))
    If mPages.Exists(bank) Then page = CLng(mPages(bank))
    page = page + CLng(parts(1)): If page < 0 Then page = 0
    mPages(bank) = page: RenderBank bank
End Sub
Public Sub QsanUi_ResetPages()
    If Not mPages Is Nothing Then mPages.RemoveAll
End Sub
Public Sub QsanUi_RowClick()
    Dim s As Shape, token As String, meta As Object
    Set s = ActiveSheet.Shapes(Application.Caller)
    token = s.AlternativeText
    If Left$(s.Name, 9) = "QSan_PIC_" Then token = Mid$(s.Name, 10)
    If Not mRows.Exists(token) Then Exit Sub
    Set meta = mRows(token)
    QsanUi_Text GetSheet("詳情").Range("A3"), CStr(meta("detail"))
    If CStr(meta("action")) = "detail" Or CStr(meta("action")) = "log" Then Exit Sub
    If Not CBool(meta("enabled")) Then QsanUi_Status "此項不可選": Exit Sub
    If CStr(meta("action")) = "mode" Or CStr(meta("action")) = "avatar" Or CStr(meta("action")) = "package" Then
        QsanCatalog_Select CStr(meta("action")), CStr(meta("key")): Exit Sub
    End If
    QsanInteraction_Action CStr(meta("action")), CStr(meta("key")), CLng(meta("instance"))
    If CStr(meta("action")) = "card" Or CStr(meta("action")) = "skill" Or CStr(meta("action")) = "target" Then
        Dim d As Object: Set d = QsanDict()
        d.Add "kind", IIf(CStr(meta("action")) = "target", "player", CStr(meta("action")))
        d.Add "key", CStr(meta("key")): QsanQueueCommand "details", d
    End If
End Sub
Private Sub RenderDetails(ByVal details As Object)
    Dim rows As Collection: Set rows = New Collection
    DetailRows details, "", rows
    QsanUi_Bank "details", rows, "詳情", "detail", 1, 8, "詳情"
    Dim pic As Shape, path As String
    Set pic = PoolShape(GetSheet("詳情"), "DETAIL_IMAGE"): path = QsanText(details, "image")
    pic.Visible = msoFalse
    If Len(path) > 0 Then
        On Error Resume Next
        If pic.AlternativeText <> path Then pic.Fill.UserPicture path
        If Err.Number = 0 Then
            pic.AlternativeText = path: pic.Left = GetSheet("詳情").Range("D8").Left
            pic.Top = GetSheet("詳情").Range("D8").Top: pic.Width = 180: pic.Height = 230: pic.Visible = msoTrue
        End If
        Err.Clear: On Error GoTo 0
    End If
End Sub
Private Sub DetailRows(ByVal source As Object, ByVal prefix As String, ByVal rows As Collection)
    Dim key As Variant, item As Object, i As Long, value As Variant
    If TypeName(source) = "Collection" Then
        For i = 1 To source.Count
            If IsObject(source(i)) Then
                DetailRows source(i), prefix & CStr(i) & "/", rows
            Else
                Set item = QsanDict(): item.Add "id", CStr(rows.Count + 1)
                item.Add "label", prefix & CStr(i) & "=" & CStr(source(i))
                item.Add "detail", item("label"): rows.Add item
            End If
        Next i
    Else
        For Each key In source.Keys
            If IsObject(source(key)) Then
                DetailRows source(key), prefix & CStr(key) & "/", rows
            ElseIf Not IsNull(source(key)) Then
                Set item = QsanDict(): item.Add "id", CStr(rows.Count + 1)
                item.Add "label", prefix & CStr(key) & "=" & CStr(source(key))
                item.Add "detail", item("label"): rows.Add item
            End If
        Next key
    End If
End Sub
Public Sub QsanUi_Text(ByVal cell As Range, ByVal value As String)
    If CStr(cell.Value2) <> value Then cell.NumberFormat = "@": cell.Value2 = value
End Sub
Public Sub QsanUi_Button(ByVal sheet As String, ByVal caption As String, ByVal action As String, ByVal anchor As String)
    Dim ws As Worksheet, s As Shape: Set ws = GetSheet(sheet): Set s = PoolShape(ws, "BTN_" & action)
    s.Left = ws.Range(anchor).Left: s.Top = ws.Range(anchor).Top: s.Width = ws.Columns(ws.Range(anchor).Column).Width - 2: s.Height = 19
    If s.TextFrame.Characters.Text <> caption Then s.TextFrame.Characters.Text = caption
    s.OnAction = MacroName(action): s.Visible = msoTrue
End Sub
Private Function PoolShape(ByVal ws As Worksheet, ByVal key As String) As Shape
    On Error Resume Next: Set PoolShape = ws.Shapes("QSan_" & key): On Error GoTo 0
    If PoolShape Is Nothing Then
        Set PoolShape = ws.Shapes.AddShape(1, 0, 0, 80, 18)
        PoolShape.Name = "QSan_" & key
        PoolShape.TextFrame.Characters.Font.Size = 9
        PoolShape.TextFrame.MarginLeft = 2: PoolShape.TextFrame.MarginTop = 0
    End If
End Function
Private Function GetSheet(ByVal name As String) As Worksheet
    On Error Resume Next: Set GetSheet = ThisWorkbook.Worksheets(name): On Error GoTo 0
    If GetSheet Is Nothing Then Set GetSheet = ThisWorkbook.Worksheets.Add: GetSheet.Name = name
End Function
Private Function MacroName(ByVal action As String) As String
    MacroName = "'" & Replace$(ThisWorkbook.Name, "'", "''") & "'!" & action
End Function
Private Sub EnsureStarted(): If Not QsanIsRunning Then QsanStartLocal
End Sub
Private Function IdentityFields() As Object
    Dim d As Object: Set d = QsanDict()
    d.Add "name", CStr(GetSheet("首頁").Range("B5").Value2)
    d.Add "avatar", CStr(GetSheet("首頁").Range("B6").Value2)
    Set IdentityFields = d
End Function
Private Sub SendFlag(ByVal command As String, ByVal key As String, ByVal value As Boolean)
    Dim d As Object: Set d = QsanDict(): d.Add key, value: QsanQueueCommand command, d
End Sub
Private Function QsanObject(ByVal o As Object, ByVal key As String) As Object
    On Error Resume Next: Set QsanObject = o(key): On Error GoTo 0
End Function
Private Function QsanText(ByVal o As Object, ByVal key As String) As String
    On Error Resume Next: QsanText = CStr(o(key)): On Error GoTo 0
End Function
