Attribute VB_Name = "QsanCatalog"
Option Explicit
Private mCatalog As Object, mSettings As Object, mLeaves As Collection, mPage As Long
Private Const PAGE_SIZE As Long = 18

Public Sub QsanCatalog_Reset()
    Set mCatalog = Nothing: Set mSettings = Nothing: Set mLeaves = Nothing: mPage = 0
End Sub
Public Sub QsanCatalog_Apply(ByVal catalog As Object)
    If catalog Is Nothing Then Exit Sub
    Set mCatalog = catalog
    ' Refreshing a catalog must never overwrite room edits already entered.
    If mSettings Is Nothing Then
        Set mSettings = QsanObject(catalog, "settings")
        If mSettings Is Nothing Then Set mSettings = QsanDict()
        Set mLeaves = New Collection: Flatten mSettings, ""
    End If
End Sub
Public Function QsanCatalog_Get(ByVal key As String) As Object
    Set QsanCatalog_Get = QsanObject(mCatalog, key)
End Function
Public Function QsanCatalog_Has(ByVal key As String) As Boolean
    If mCatalog Is Nothing Then Exit Function
    QsanCatalog_Has = mCatalog.Exists(key)
End Function
Public Function QsanCatalog_RuntimeTier() As String
    QsanCatalog_RuntimeTier = QsanText(mCatalog, "runtime_tier")
End Function
Public Function QsanCatalog_MaxPlayers() As Long
    QsanCatalog_MaxPlayers = CLng(Val(QsanText(mCatalog, "max_players")))
End Function
Public Sub QsanCatalog_RenderSettings(ByVal ws As Worksheet)
    If mLeaves Is Nothing Then Exit Sub
    QsanUi_Text ws.Range("A2"), "房間設定：修改值後按套用；清單每行一項"
    QsanUi_Button ws.Name, "套用", "QsanCatalog_Save", "A3"
    QsanUi_Button ws.Name, "上一頁", "QsanCatalog_Prev", "C3"
    QsanUi_Button ws.Name, "下一頁", "QsanCatalog_Next", "E3"
    Dim i As Long, n As Long, leaf As Object, parent As Object, key As String, value As Variant
    For i = 1 To PAGE_SIZE
        n = mPage * PAGE_SIZE + i
        If n <= mLeaves.Count Then
            Set leaf = mLeaves(n): Set parent = leaf("parent"): key = CStr(leaf("key"))
            QsanUi_Text ws.Cells(i + 5, 1), CStr(leaf("path"))
            QsanUi_Text ws.Cells(i + 5, 4), CStr(leaf("type"))
            If IsObject(parent(key)) Then
                leaf("display") = ListText(parent(key))
                If CStr(leaf("type")) = "StructuredList" Then leaf("display") = "[結構清單：保留原值]"
            Else
                leaf("display") = CStr(parent(key))
            End If
            QsanUi_Text ws.Cells(i + 5, 3), CStr(leaf("display"))
        Else
            ws.Range(ws.Cells(i + 5, 1), ws.Cells(i + 5, 4)).ClearContents
        End If
    Next i
    QsanUi_Bank "catalog_modes", QsanCatalog_Get("modes"), "模式", "mode", 5, 8, "房間設定"
    QsanUi_Bank "catalog_packages", QsanCatalog_Get("packages"), "套件", "package", 5, 20, "房間設定"
    QsanUi_Bank "catalog_generals", QsanCatalog_Get("generals"), "頭像 / 武將", "avatar", 7, 8, "房間設定"
End Sub
Public Sub QsanCatalog_Save()
    If mLeaves Is Nothing Then Exit Sub
    Dim ws As Worksheet: Set ws = ThisWorkbook.Worksheets("房間設定")
    Dim i As Long, n As Long, leaf As Object, parent As Object, key As String, text As String, list As Collection, x As Variant
    For i = 1 To PAGE_SIZE
        n = mPage * PAGE_SIZE + i: If n > mLeaves.Count Then Exit For
        Set leaf = mLeaves(n): Set parent = leaf("parent"): key = CStr(leaf("key"))
        text = CStr(ws.Cells(i + 5, 3).Value2)
        If leaf.Exists("display") Then
            If text = CStr(leaf("display")) Then GoTo nextValue
        End If
        Select Case CStr(leaf("type"))
        Case "Boolean"
            If LCase$(text) <> "true" And LCase$(text) <> "false" Then Err.Raise 5, , "設定需為 true/false: " & key
            parent(key) = LCase$(text) = "true"
        Case "Number"
            If Not IsNumeric(text) Then Err.Raise 5, , "設定需為數字: " & key
            parent(key) = CDbl(text)
        Case "StructuredList"
            Err.Raise 5, , "此結構清單只能保留原值: " & key
        Case "List"
            Set list = New Collection
            For Each x In Split(Replace$(text, vbCr, ""), vbLf)
                If Len(CStr(x)) > 0 Then list.Add CStr(x)
            Next x
            Set parent(key) = list
        Case Else: parent(key) = text
        End Select
        leaf("display") = text
nextValue:
    Next i
End Sub
Public Sub QsanCatalog_Next()
    QsanCatalog_Save
    If (mPage + 1) * PAGE_SIZE < mLeaves.Count Then mPage = mPage + 1
    QsanCatalog_RenderSettings ThisWorkbook.Worksheets("房間設定")
End Sub
Public Sub QsanCatalog_Prev()
    QsanCatalog_Save
    If mPage > 0 Then mPage = mPage - 1
    QsanCatalog_RenderSettings ThisWorkbook.Worksheets("房間設定")
End Sub
Public Function QsanCatalog_Settings() As Object
    QsanCatalog_Save
    If mSettings Is Nothing Then Set mSettings = QsanDict()
    Set QsanCatalog_Settings = mSettings
End Function
Public Sub QsanCatalog_Select(ByVal action As String, ByVal key As String)
    If action = "avatar" Then
        QsanUi_Text ThisWorkbook.Worksheets("首頁").Range("B6"), key
    ElseIf action = "package" Then
        QsanCatalog_Save
        Dim bans As Object, updated As Collection, x As Variant, found As Boolean
        Set bans = QsanObject(mSettings, "BanPackages"): Set updated = New Collection
        If Not bans Is Nothing Then
            For Each x In bans
                If CStr(x) = key Then found = True Else updated.Add CStr(x)
            Next x
        End If
        If Not found Then updated.Add key
        Set mSettings("BanPackages") = updated
        QsanUi_Status IIf(found, "已啟用套件: ", "已停用套件: ") & key
        QsanCatalog_RenderSettings ThisWorkbook.Worksheets("房間設定")
    ElseIf action = "mode" Then
        QsanCatalog_Save
        If mSettings Is Nothing Then Set mSettings = QsanDict()
        mSettings("GameMode") = key
        QsanCatalog_RenderSettings ThisWorkbook.Worksheets("房間設定")
    End If
End Sub
Private Sub Flatten(ByVal values As Object, ByVal prefix As String)
    Dim key As Variant, leaf As Object, valueType As String, member As Variant
    For Each key In values.Keys
        If IsObject(values(key)) Then
            If TypeName(values(key)) = "Dictionary" Then
                Flatten values(key), prefix & CStr(key) & "/"
                GoTo nextKey
            End If
            valueType = "List"
            For Each member In values(key)
                If IsObject(member) Then valueType = "StructuredList"
            Next member
        ElseIf VarType(values(key)) = vbBoolean Then
            valueType = "Boolean"
        ElseIf VarType(values(key)) <> vbString And IsNumeric(values(key)) Then
            valueType = "Number"
        Else
            valueType = "Text"
        End If
        Set leaf = QsanDict(): leaf.Add "parent", values: leaf.Add "key", CStr(key)
        leaf.Add "path", prefix & CStr(key): leaf.Add "type", valueType: mLeaves.Add leaf
nextKey:
    Next key
End Sub
Private Function ListText(ByVal list As Object) As String
    Dim x As Variant
    For Each x In list
        If Not IsObject(x) Then
            If Len(ListText) > 0 Then ListText = ListText & vbLf
            ListText = ListText & CStr(x)
        End If
    Next x
End Function
Private Function QsanObject(ByVal o As Object, ByVal key As String) As Object
    On Error Resume Next: Set QsanObject = o(key): On Error GoTo 0
End Function
Private Function QsanText(ByVal o As Object, ByVal key As String) As String
    On Error Resume Next: QsanText = CStr(o(key)): On Error GoTo 0
End Function
