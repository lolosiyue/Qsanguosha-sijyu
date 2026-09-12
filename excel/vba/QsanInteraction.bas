Attribute VB_Name = "QsanInteraction"
Option Explicit

Private mRequest As Object, mPayload As Object, mUi As Object
Private mId As String, mGeneration As String, mRevision As String, mShape As String
Private mCards As Collection, mTargets As Collection, mTop As Collection, mBottom As Collection, mOrder As Collection
Private mAssignments As Object, mOption As String, mAssignPlayer As String
Private mSkill As String, mInstance As Long, mDeclaration As String, mLane As String
Private mApproved As String, mCancelable As Boolean

Public Sub QsanInteraction_Initialize()
    Set mCards = New Collection: Set mTargets = New Collection
    Set mTop = New Collection: Set mBottom = New Collection: Set mOrder = New Collection
    Set mAssignments = QsanDict()
    mOption = "": mAssignPlayer = "": mSkill = "": mInstance = 0: mDeclaration = ""
    mApproved = "": mLane = "top"
End Sub

Public Sub QsanInteraction_Apply(ByVal interaction As Object, ByVal ws As Worksheet)
    Dim identityChanged As Boolean
    If interaction Is Nothing Then Set interaction = QsanDict()
    identityChanged = (QsanText(interaction, "request_id") <> mId Or QsanText(interaction, "generation") <> mGeneration)
    If identityChanged Or mCards Is Nothing Then QsanInteraction_Initialize
    If QsanText(interaction, "revision") <> mRevision Then mApproved = ""
    Set mRequest = interaction: Set mPayload = QsanObject(interaction, "payload")
    Set mUi = QsanObject(interaction, "ui")
    mId = QsanText(interaction, "request_id"): mGeneration = QsanText(interaction, "generation")
    mRevision = QsanText(interaction, "revision"): mShape = QsanText(interaction, "response_schema")
    mCancelable = (LCase$(QsanText(interaction, "cancelable")) = "true")
    QsanUi_Text ws.Range("A2"), QsanText(interaction, "prompt")
    QsanUi_Text ws.Range("A3"), QsanText(interaction, "type") & "  " & QsanText(interaction, "remaining_ms") & " ms"
    If identityChanged Then QsanUi_ResetPages
    QsanInteraction_Render
    QsanUi_InteractionButtons Len(mApproved) > 0, mCancelable
    If mShape = "custom" Then QsanUi_Status "此 QML 互動尚無可驗證的工作表描述器"
End Sub

Public Sub QsanInteraction_Render()
    Dim options As Object, cards As Object, players As Object, skills As Object
    Set options = QsanObject(mUi, "options"): Set cards = QsanObject(mUi, "cards")
    Set players = QsanObject(mUi, "players"): Set skills = QsanObject(mUi, "skills")
    If options Is Nothing Then Set options = QsanObject(mPayload, "options")
    If cards Is Nothing Then Set cards = QsanObject(mPayload, "selectable_cards")
    If cards Is Nothing Then Set cards = QsanObject(mPayload, "cards")
    If players Is Nothing Then Set players = QsanObject(mPayload, "selectable_players")
    If players Is Nothing Then Set players = QsanObject(mPayload, "target_players")
    If skills Is Nothing Then Set skills = QsanObject(mRequest, "skill_choices")
    If mShape = "assignment" Then
        Set players = QsanObject(mPayload, "players"): Set options = QsanObject(mPayload, "roles")
    ElseIf mShape = "general_arrangement" Then
        Set options = QsanObject(mPayload, "generals")
    End If
    QsanUi_Bank "options", options, "選項 / 排列", "option", 1, 8
    QsanUi_Bank "cards", cards, "互動牌", "card", 3, 8
    QsanUi_Bank "targets", players, "目標（依序加入）", "target", 5, 8
    QsanUi_Bank "skills", skills, "技能實例", "skill", 7, 8
    QsanUi_Bank "declarations", QsanObject(mUi, "declarations"), "宣告", "declaration", 7, 20
    QsanUi_Text ThisWorkbook.Worksheets("牌桌").Range("A32"), "牌: " & JoinItems(mCards) & "  目標: " & JoinItems(mTargets)
    QsanUi_Text ThisWorkbook.Worksheets("牌桌").Range("A33"), "上: " & JoinItems(mTop) & " 下: " & JoinItems(mBottom) & " 排列: " & JoinItems(mOrder)
    QsanUi_Text ThisWorkbook.Worksheets("牌桌").Range("A34"), "選項: " & mOption & "  指派: " & AssignmentText() & "  目前玩家: " & mAssignPlayer & " 技能: " & mSkill & " 宣告: " & mDeclaration
End Sub

Public Sub QsanInteraction_Action(ByVal action As String, ByVal key As String, Optional ByVal instance As Long = 0)
    If Len(mId) = 0 Then Exit Sub
    Select Case action
    Case "card"
        If mShape = "rearrangement" Then
            RemoveValue mTop, key: RemoveValue mBottom, key
            If mLane = "top" Then mTop.Add CLng(key) Else mBottom.Add CLng(key)
        Else
            If Contains(mCards, key) Then RemoveValue mCards, key Else mCards.Add CLng(key)
        End If
    Case "target"
        If mShape = "assignment" Then
            mAssignPlayer = key
        ElseIf mShape = "distribution" Then
            Set mTargets = New Collection: mTargets.Add key
        Else
            ' Ordered Collection deliberately retains repeated target votes.
            mTargets.Add key
        End If
    Case "option"
        If mShape = "assignment" Then
            If Len(mAssignPlayer) > 0 Then mAssignments(mAssignPlayer) = key
        ElseIf mShape = "general_arrangement" Then
            RemoveValue mOrder, key: mOrder.Add key
        Else
            mOption = key
        End If
    Case "skill"
        If mSkill = key And mInstance = instance Then
            mSkill = "": mInstance = 0
        Else
            mSkill = key: mInstance = instance
        End If
        mDeclaration = ""
    Case "declaration": mDeclaration = key
    End Select
    QsanInteraction_Preview
    QsanInteraction_Render
    QsanUi_InteractionButtons Len(mApproved) > 0, mCancelable
    If mShape = "custom" Then QsanUi_Status "此 QML 互動尚無可驗證的工作表描述器"
End Sub

Public Sub QsanInteraction_Click(ByVal key As String, ByVal row As Long): QsanInteraction_Action "option", key: End Sub
Public Sub QsanInteraction_Target(ByVal key As String): QsanInteraction_Action "target", key: End Sub
Public Sub QsanInteraction_Top(): mLane = "top": QsanUi_Status "點牌依序放到牌堆頂": End Sub
Public Sub QsanInteraction_Bottom(): mLane = "bottom": QsanUi_Status "點牌依序放到牌堆底": End Sub
Public Sub QsanInteraction_UndoTarget()
    If mTargets.Count > 0 Then mTargets.Remove mTargets.Count
    QsanInteraction_Preview: QsanInteraction_Render
End Sub
Public Sub QsanInteraction_Clear()
    QsanInteraction_Initialize: QsanInteraction_Preview: QsanInteraction_Render
End Sub
Public Sub QsanInteraction_Preview()
    mApproved = ""
    QsanUi_InteractionButtons False, mCancelable
    If mShape = "custom" Then QsanUi_Status "此 QML 互動尚無可驗證的工作表描述器": Exit Sub
    If Len(mId) > 0 Then QsanInteraction_Send "select", QsanInteraction_Draft(), True
End Sub
Public Sub QsanInteraction_Confirm()
    If Len(mId) = 0 Then Exit Sub
    Dim d As Object: Set d = QsanInteraction_Draft()
    If mApproved <> QsanJsonStringify(d) Or Len(mApproved) = 0 Then
        QsanUi_Status "請先預檢；等待目前選擇通過": QsanInteraction_Preview: Exit Sub
    End If
    mApproved = "": QsanInteraction_Send "submit", d
End Sub
Public Sub QsanInteraction_Cancel()
    If Len(mId) = 0 Or Not mCancelable Then Exit Sub
    Dim a As Object: Set a = IdentityArgs()
    mApproved = "": QsanQueueCommand "cancel", a
End Sub
Public Sub QsanInteraction_EndTurn()
    If QsanText(mRequest, "type") = "play_card" Then QsanInteraction_Cancel
End Sub

Public Sub QsanInteraction_Result(ByVal commandName As String, ByVal args As Object, ByVal envelope As Object)
    If commandName <> "select" Then Exit Sub
    If QsanText(args, "request_id") <> mId Or QsanText(args, "generation") <> mGeneration Or QsanText(args, "revision") <> mRevision Then Exit Sub
    Dim draft As Object, result As Object, selection As Object
    Set draft = QsanObject(args, "draft"): Set result = QsanObject(envelope, "result")
    If draft Is Nothing Or result Is Nothing Then Exit Sub
    Set selection = QsanObject(result, "selection"): If selection Is Nothing Then Set selection = result
    If QsanJsonStringify(draft) <> QsanJsonStringify(QsanInteraction_Draft()) Then Exit Sub
    mApproved = ""
    If Not QsanObject(selection, "ui") Is Nothing Then Set mUi = QsanObject(selection, "ui"): QsanInteraction_Render
    If LCase$(QsanText(envelope, "ok")) = "true" And LCase$(QsanText(selection, "can_confirm")) = "true" Then
        mApproved = QsanJsonStringify(draft): QsanUi_Status "目前選擇已通過，可確認"
        QsanUi_InteractionButtons True, mCancelable
    Else
        QsanUi_Status QsanText(selection, "reason")
    End If
End Sub
Private Function IdentityArgs() As Object
    Dim a As Object: Set a = QsanDict()
    a.Add "request_id", mId: a.Add "generation", mGeneration: a.Add "revision", mRevision
    Set IdentityArgs = a
End Function
Private Sub QsanInteraction_Send(ByVal commandName As String, ByVal draft As Object, Optional ByVal preflight As Boolean = False)
    Dim a As Object: Set a = IdentityArgs(): a.Add "draft", draft
    QsanQueueCommand commandName, a, preflight
End Sub
Private Function QsanInteraction_Draft() As Object
    Dim d As Object: Set d = QsanDict()
    Select Case mShape
    Case "cards"
        d.Add "cards", mCards: d.Add "targets", mTargets
        d.Add "skill_name", mSkill: d.Add "skill_instance_id", mInstance: d.Add "declaration", mDeclaration
    Case "players": d.Add "targets", mTargets
    Case "option": d.Add "option", mOption
    Case "assignment": d.Add "assignments", mAssignments
    Case "rearrangement": d.Add "top", mTop: d.Add "bottom", mBottom
    Case "distribution"
        d.Add "cards", mCards: d.Add "targets", mTargets
    Case "general_arrangement": d.Add "order", mOrder
    Case "custom"
        ' Arbitrary QML has no worksheet schema; never fabricate a JSON response.
    End Select
    Set QsanInteraction_Draft = QsanJsonParse(QsanJsonStringify(d))
End Function
Private Function AssignmentText() As String
    Dim key As Variant
    For Each key In mAssignments.Keys
        AssignmentText = AssignmentText & CStr(key) & "=" & CStr(mAssignments(key)) & " "
    Next key
End Function
Private Function Contains(ByVal list As Collection, ByVal key As String) As Boolean
    Dim x As Variant
    For Each x In list
        If CStr(x) = key Then Contains = True: Exit Function
    Next x
End Function
Private Sub RemoveValue(ByVal list As Collection, ByVal key As String)
    Dim i As Long
    For i = list.Count To 1 Step -1
        If CStr(list(i)) = key Then list.Remove i
    Next i
End Sub
Private Function JoinItems(ByVal list As Collection) As String
    Dim x As Variant
    For Each x In list
        JoinItems = JoinItems & CStr(x) & " "
    Next x
End Function
' Helpers tolerate absent optional fields without converting ID strings to numbers.
Private Function QsanObject(ByVal o As Object, ByVal key As String) As Object
    On Error Resume Next: Set QsanObject = o(key): On Error GoTo 0
End Function
Private Function QsanText(ByVal o As Object, ByVal key As String) As String
    On Error Resume Next: QsanText = CStr(o(key)): On Error GoTo 0
End Function
