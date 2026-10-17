-- Actual Lua 5.4 parser, VM, coroutine and collector. Integer grid, not Minecraft.
local function chunk(seed)
  local blocks, checksum = {}, 0
  for z=1,16 do
    blocks[z] = {}
    for x=1,16 do
      seed = (seed * 1664525 + 1013904223) & 0xffffffff
      blocks[z][x] = seed % 64
      checksum = checksum + blocks[z][x]
    end
  end
  return blocks, checksum
end
local blocks, total = chunk(21)
assert(#blocks==16 and #blocks[1]==16 and total==8064)
local task=coroutine.create(function() coroutine.yield('ready');return total end)
local ok,value=coroutine.resume(task);assert(ok and value=='ready')
ok,value=coroutine.resume(task);assert(ok and value==8064)
local success,message=pcall(function() error('caught Lua error') end)
assert(not success and message:find('caught Lua error'))
assert(math.sqrt(81)==9 and math.sin(0)==0)
local file=assert(io.open('lua-world.txt','w'));file:write('Lua persistence: '..total..'\n');assert(file:close())
file=assert(io.open('lua-world.txt','r'));assert(file:read('*a')=='Lua persistence: 8064\n');assert(file:close())
for i=1,1000 do local garbage={string.rep('block',100),i} end
collectgarbage('collect')
print(_VERSION..' / tables, coroutines, GC, errors, floats and files')
print('chunk=16x16, checksum='..total)
print('arg='..(arg[1] or 'none'))
print('LUA RUNTIME PASS')
