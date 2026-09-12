Attribute VB_Name = "QsanJson"
Option Explicit

' Strict JSON without eval. Protocol identifiers stay JSON strings throughout.
Private mText As String
Private mPos As Long
Private Const MAX_JSON As Long = 4194304
Private Const MAX_DEPTH As Long = 32
Private Const MAX_SAFE_INTEGER As Double = 9007199254740991#

Public Function QsanJsonParse(ByVal text As String) As Object
    If Len(text) > MAX_JSON Then JsonError "json_too_large"
    mText = text
    mPos = 1
    SkipWs
    If Not IsContainer Then JsonError "json_root_object_required"
    Set QsanJsonParse = ParseValue(0)
    SkipWs
    If mPos <= Len(mText) Then JsonError "json_trailing_data"
End Function

Private Function ParseValue(ByVal depth As Long) As Variant
    If depth > MAX_DEPTH Then JsonError "json_depth"
    SkipWs
    Select Case Mid$(mText, mPos, 1)
    Case "{"
        Set ParseValue = ParseObject(depth + 1)
    Case "["
        Set ParseValue = ParseArray(depth + 1)
    Case """"
        ParseValue = ParseString()
    Case "t"
        ConsumeLiteral "true"
        ParseValue = True
    Case "f"
        ConsumeLiteral "false"
        ParseValue = False
    Case "n"
        ConsumeLiteral "null"
        ParseValue = Null
    Case Else
        ParseValue = ParseNumber()
    End Select
End Function

Private Function ParseObject(ByVal depth As Long) As Object
    Dim d As Object, key As String, child As Object
    Set d = CreateObject("Scripting.Dictionary")
    d.CompareMode = vbBinaryCompare
    mPos = mPos + 1
    SkipWs
    If Peek("}") Then
        mPos = mPos + 1
        Set ParseObject = d
        Exit Function
    End If
    Do
        SkipWs
        key = ParseString()
        If d.Exists(key) Then JsonError "json_duplicate_key"
        SkipWs
        If Not Peek(":") Then JsonError "json_object_colon"
        mPos = mPos + 1
        SkipWs
        If IsContainer Then
            Set child = ParseValue(depth)
            d.Add key, child
        Else
            d.Add key, ParseValue(depth)
        End If
        SkipWs
        If Peek("}") Then
            mPos = mPos + 1
            Exit Do
        End If
        If Not Peek(",") Then JsonError "json_object_comma"
        mPos = mPos + 1
    Loop
    Set ParseObject = d
End Function

Private Function ParseArray(ByVal depth As Long) As Collection
    Dim values As New Collection, child As Object
    mPos = mPos + 1
    SkipWs
    If Peek("]") Then
        mPos = mPos + 1
        Set ParseArray = values
        Exit Function
    End If
    Do
        SkipWs
        If IsContainer Then
            Set child = ParseValue(depth)
            values.Add child
        Else
            values.Add ParseValue(depth)
        End If
        SkipWs
        If Peek("]") Then
            mPos = mPos + 1
            Exit Do
        End If
        If Not Peek(",") Then JsonError "json_array_comma"
        mPos = mPos + 1
    Loop
    Set ParseArray = values
End Function

Private Function ParseString() As String
    Dim result As String, ch As String, esc As String, code As Long, low As Long
    If Not Peek("""") Then JsonError "json_string"
    mPos = mPos + 1
    Do While mPos <= Len(mText)
        ch = Mid$(mText, mPos, 1)
        mPos = mPos + 1
        If ch = """" Then
            ParseString = result
            Exit Function
        End If
        code = AscW(ch) And &HFFFF&
        If code < 32 Then JsonError "json_control"
        If ch = "\" Then
            If mPos > Len(mText) Then JsonError "json_escape"
            esc = Mid$(mText, mPos, 1)
            mPos = mPos + 1
            Select Case esc
            Case """", "\", "/"
                result = result & esc
            Case "b"
                result = result & Chr$(8)
            Case "f"
                result = result & Chr$(12)
            Case "n"
                result = result & vbLf
            Case "r"
                result = result & vbCr
            Case "t"
                result = result & vbTab
            Case "u"
                code = ReadHexUnit()
                If code >= &HD800& And code <= &HDBFF& Then
                    If Mid$(mText, mPos, 2) <> "\u" Then JsonError "json_surrogate"
                    mPos = mPos + 2
                    low = ReadHexUnit()
                    If low < &HDC00& Or low > &HDFFF& Then JsonError "json_surrogate"
                    result = result & Utf16Unit(code) & Utf16Unit(low)
                ElseIf code >= &HDC00& And code <= &HDFFF& Then
                    JsonError "json_surrogate"
                Else
                    result = result & Utf16Unit(code)
                End If
            Case Else
                JsonError "json_escape"
            End Select
        ElseIf code >= &HD800& And code <= &HDBFF& Then
            If mPos > Len(mText) Then JsonError "json_surrogate"
            low = AscW(Mid$(mText, mPos, 1)) And &HFFFF&
            If low < &HDC00& Or low > &HDFFF& Then JsonError "json_surrogate"
            result = result & ch & Mid$(mText, mPos, 1)
            mPos = mPos + 1
        ElseIf code >= &HDC00& And code <= &HDFFF& Then
            JsonError "json_surrogate"
        Else
            result = result & ch
        End If
    Loop
    JsonError "json_unclosed_string"
End Function

Private Function ReadHexUnit() As Long
    Dim i As Long, digit As Long, result As Long
    If mPos + 3 > Len(mText) Then JsonError "json_unicode"
    For i = 0 To 3
        digit = InStr(1, "0123456789abcdef", LCase$(Mid$(mText, mPos + i, 1)), vbBinaryCompare) - 1
        If digit < 0 Then JsonError "json_unicode"
        result = result * 16 + digit
    Next i
    mPos = mPos + 4
    ReadHexUnit = result
End Function

Private Function Utf16Unit(ByVal code As Long) As String
    If code > 32767 Then code = code - 65536
    Utf16Unit = ChrW$(code)
End Function

Private Sub ConsumeLiteral(ByVal word As String)
    If Mid$(mText, mPos, Len(word)) <> word Then JsonError "json_literal"
    mPos = mPos + Len(word)
End Sub

Private Function ParseNumber() As Double
    Dim first As Long, text As String, value As Double
    first = mPos
    If Peek("-") Then mPos = mPos + 1
    If Not IsDigit Then JsonError "json_number"
    If Peek("0") Then
        mPos = mPos + 1
        If IsDigit Then JsonError "json_number"
    Else
        Do While IsDigit
            mPos = mPos + 1
        Loop
    End If
    If Peek(".") Then
        mPos = mPos + 1
        If Not IsDigit Then JsonError "json_number"
        Do While IsDigit
            mPos = mPos + 1
        Loop
    End If
    If Peek("e") Or Peek("E") Then
        mPos = mPos + 1
        If Peek("+") Or Peek("-") Then mPos = mPos + 1
        If Not IsDigit Then JsonError "json_number"
        Do While IsDigit
            mPos = mPos + 1
        Loop
    End If
    text = Mid$(mText, first, mPos - first)
    ' Val/Str use a period regardless of the Office locale. IDs must be strings.
    On Error GoTo badNumber
    value = Val(text)
    If Abs(value) > MAX_SAFE_INTEGER And value = Fix(value) Then JsonError "json_unsafe_integer"
    ParseNumber = value
    Exit Function
badNumber:
    JsonError "json_number_range"
End Function

Private Function IsDigit() As Boolean
    Dim ch As String
    ch = Mid$(mText, mPos, 1)
    If Len(ch) = 1 Then IsDigit = ch >= "0" And ch <= "9"
End Function

Private Function IsContainer() As Boolean
    IsContainer = Peek("{") Or Peek("[")
End Function

Private Sub SkipWs()
    Do While mPos <= Len(mText)
        Select Case Mid$(mText, mPos, 1)
        Case " ", vbTab, vbCr, vbLf
            mPos = mPos + 1
        Case Else
            Exit Do
        End Select
    Loop
End Sub

Private Function Peek(ByVal token As String) As Boolean
    Peek = Mid$(mText, mPos, 1) = token
End Function

Public Function QsanJsonStringify(ByVal value As Variant) As String
    QsanJsonStringify = JsonOut(value, 0)
    If Len(QsanJsonStringify) > MAX_JSON Then JsonError "json_too_large"
End Function

Private Function JsonOut(ByVal value As Variant, ByVal depth As Long) As String
    Dim key As Variant, i As Long, result As String, kind As String, number As Double
    If depth > MAX_DEPTH Then JsonError "json_depth"
    If IsObject(value) Then
        kind = TypeName(value)
        Select Case kind
        Case "Dictionary", "Scripting.Dictionary"
            result = "{"
            For Each key In value.Keys
                If VarType(key) <> vbString Then JsonError "json_key_type"
                If Len(result) > 1 Then result = result & ","
                result = result & """" & EscapeJson(CStr(key)) & """:" & JsonOut(value(key), depth + 1)
            Next key
            JsonOut = result & "}"
        Case "Collection"
            result = "["
            For i = 1 To value.Count
                If i > 1 Then result = result & ","
                result = result & JsonOut(value(i), depth + 1)
            Next i
            JsonOut = result & "]"
        Case Else
            JsonError "json_object_type"
        End Select
        Exit Function
    End If
    Select Case VarType(value)
    Case vbNull, vbEmpty
        JsonOut = "null"
    Case vbString
        ' Never use IsNumeric here: "18446744073709551615" is a string ID.
        JsonOut = """" & EscapeJson(CStr(value)) & """"
    Case vbBoolean
        If value Then
            JsonOut = "true"
        Else
            JsonOut = "false"
        End If
    Case vbByte, vbInteger, vbLong, vbSingle, vbDouble, vbCurrency, vbDecimal, 20
        number = CDbl(value)
        If Abs(number) > MAX_SAFE_INTEGER And number = Fix(number) Then JsonError "json_unsafe_integer"
        If number = Fix(number) Then
            ' Str(Double) may round 16-digit safe integers into scientific notation.
            JsonOut = Format$(number, "0")
        Else
            result = Trim$(Str$(number))
            If Left$(result, 1) = "." Then result = "0" & result
            If Left$(result, 2) = "-." Then result = "-0" & Mid$(result, 2)
            JsonOut = result
        End If
    Case Else
        JsonError "json_value_type"
    End Select
End Function

Private Function EscapeJson(ByVal text As String) As String
    Dim i As Long, code As Long, low As Long, result As String
    i = 1
    Do While i <= Len(text)
        code = AscW(Mid$(text, i, 1)) And &HFFFF&
        Select Case code
        Case 34
            result = result & "\"""
        Case 92
            result = result & "\\"
        Case 32 To 33, 35 To 91, 93 To 126
            result = result & Chr$(code)
        Case Else
            If code >= &HD800& And code <= &HDBFF& Then
                If i = Len(text) Then JsonError "json_surrogate"
                low = AscW(Mid$(text, i + 1, 1)) And &HFFFF&
                If low < &HDC00& Or low > &HDFFF& Then JsonError "json_surrogate"
                result = result & "\u" & Right$("0000" & Hex$(code), 4)
                result = result & "\u" & Right$("0000" & Hex$(low), 4)
                i = i + 1
            ElseIf code >= &HDC00& And code <= &HDFFF& Then
                JsonError "json_surrogate"
            Else
                result = result & "\u" & Right$("0000" & Hex$(code), 4)
            End If
        End Select
        i = i + 1
    Loop
    EscapeJson = result
End Function

Private Sub JsonError(ByVal code As String)
    Err.Raise vbObjectError + 710, "QsanJson", code
End Sub
