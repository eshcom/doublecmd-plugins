-- SaveClipboardToFileCtrlV.lua (cross-platform)
-- 2022.01.27
--[[
Replacing the action of the Ctrl+V keys:
script will save text from clipboard (if exists) or call cm_PasteFromClipboard.

Internal command: cm_ExecuteScript
Parameters:
  path-to-SaveClipboardToFileCtrlV.lua
  %"0 %D
  --move-cursor

"--move-cursor" (optional) will move the cursor to the new file.
]]

local params = {...}

local d = Clipbrd.GetAsText()
if string.len(d) > 0 then
  local tf = params[1] .. SysUtils.PathDelim .. "Clipboard Text.txt"
  if SysUtils.FileExists(tf) == true then
    local c = 2
    while true do
      tf = params[1] .. SysUtils.PathDelim .. "Clipboard Text(" .. c .. ").txt"
      if SysUtils.FileExists(tf) == false then
        break
      end
      c = c + 1
    end
    local h = io.open(tf, "wb")
    if h == nil then
      Dialogs.MessageBox("Unknown error.", "Save text from clipboard", 0x0030)
      return
    end
    h:write(d)
    h:close()
    if (#params == 2) and (params[2] == "--move-cursor") then
      DC.ExecuteCommand("cm_Refresh")
      DC.ExecuteCommand("cm_QuickSearch", "search=on", "direction=first", "matchbeginning=on", "matchending=off", "casesensitive=on", "files=on", "directories=off", "text=" .. SysUtils.ExtractFileName(tf))
      DC.ExecuteCommand("cm_QuickSearch", "search=off")
    end
  end
else
  DC.ExecuteCommand("cm_PasteFromClipboard")
end
