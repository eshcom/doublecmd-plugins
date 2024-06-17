-- CreateNewFile.lua (cross-platform)
-- 2020.11.02
--
-- Create new file using the template(s)
-- Params:
--   %"0%D
--   %dir-with-templates%
--
-- Button example:
--[[
DOUBLECMD#TOOLBAR#XMLDATA<?xml version="1.0" encoding="UTF-8"?>
<doublecmd>
  <Command>
    <ID>{78AE07D0-42A0-4238-92BB-BE802AFB6F48}</ID>
    <Icon>cm_editnew</Icon>
    <Hint>Create new file</Hint>
    <Command>cm_ExecuteScript</Command>
    <Param>%COMMANDER_PATH%/utils/CreateNewFile.lua</Param>
    <Param>%"0%D</Param>
    <Param>%COMMANDER_PATH%/utils/newfiles</Param>
  </Command>
</doublecmd>
]]

local params = {...}
local items = {}
local hd, fd, rt, st, i, ba, tf, bp, h, ct
local mode = 'wb'
local sn = 'new'

if #params ~= 2 then
  Dialogs.MessageBox('Check the number of parameters!\nRequires only two.', 'Create new file', 0x0030)
  return
end
-- Get list of new files
hd, fd = SysUtils.FindFirst(params[2] .. SysUtils.PathDelim .. '*')
if hd ~= nil then
  repeat
    if (fd.Name ~= '.') and (fd.Name ~= '..') then table.insert(items, fd.Name) end
    rt, fd = SysUtils.FindNext(hd)
  until rt == nil
  SysUtils.FindClose(hd)
end
-- Get new file
if #items >= 1 then
  table.sort(items)
  st = Dialogs.InputListBox('Create new file', 'Select type:', items, 1)
  if st == nil then return end
  -- Get new filename
  while i ~= 1 do
    ba, sn = Dialogs.InputQuery('Type: ' .. st, 'Enter filename:', false, sn)
    if ba == false then return end
    tf = params[1] .. SysUtils.PathDelim .. string.gsub(st, '(.+)(%.[^%.]+)$', sn .. '%2')
    if SysUtils.FileExists(tf) == false then break end
    bp = Dialogs.MessageBox('File "' .. tf .. '" already exists!\n\nOverwrite?', 'Create new file', 0x0003 + 0x0030 + 0x0100)
    if bp == 0x0006 then
      break
    else
      if bp ~= 0x0007 then return end
    end
  end
  -- Read-copy
  h = io.open(params[2] .. SysUtils.PathDelim .. st, 'rb')
  ct = h:read('*a')
  h:close()
  -- Write-paste
  if SysUtils.FileExists(tf) == true then mode = 'w+b' end
  h = io.open(tf, mode)
  h:write(ct)
  h:close()
  DC.ExecuteCommand('cm_Refresh')
  DC.ExecuteCommand('cm_QuickSearch', 'search=on', 'direction=first', 'matchbeginning=on', 'matchending=off', 'casesensitive=on', 'files=on', 'directories=off', 'text=' .. string.gsub(st, '(.+)(%.[^%.]+)$', sn .. '%2'))
  DC.ExecuteCommand('cm_QuickSearch', 'search=off')
end
