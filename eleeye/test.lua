local engine = io.popen("./ELEEYE.EXE", "r") -- 替换为你的引擎路径
-- 1. 初始化UCCI协议（你已经完成这步）


engine:write("ucci\n")
engine:flush()
-- ... 读取直到看到 `ucciok`

-- 2. (推荐) 设置关键参数，提升AI强度
engine:write("setoption name hashsize value 256\n")
engine:flush()

-- 3. 设置棋盘局面
-- 从初始局面开始，红方先走兵七进一 (h2e2)
engine:write("position startpos moves h2e2\n")
engine:flush()

-- 4. 命令AI思考，例如：思考5秒
engine:write("go movetime 5000\n")
engine:flush()

-- 5. 读取AI的思考结果
local bestmove = nil
while true do
	local line = engine:read("*l")
	if not line then break end
	print("引擎输出:", line)

	if line:match("^bestmove") then
		-- 解析最佳着法，例如: bestmove h2e2
		bestmove = line:match("bestmove%s+(%S+)")
		break
	end
	-- 你还可以在这里解析中间信息，如: info depth 10 score 55 time 1234 nodes 567890 nps 456789
end

print("AI推荐着法: ", bestmove)

-- 6. 如果要继续，可以接着走子...
engine:write("position startpos moves h2e2 " .. bestmove .. "\n")
engine:write("go movetime 3000\n")

-- 7. 对局结束，关闭引擎
engine:write("quit\n")
engine:close()
