#g++ -DNDEBUG -O4 -Wall -oELEEYE.EXE ../base/pipe.cpp ucci.cpp pregen.cpp position.cpp genmoves.cpp hash.cpp book.cpp movesort.cpp preeval.cpp evaluate.cpp search.cpp eleeye.cpp
#编译象眼引擎，揭棋引擎 
g++ -g -w -O0 -Wall -oELEEYE.EXE ../base/pipe.cpp ucci.cpp pregen.cpp position.cpp genmoves.cpp hash.cpp book.cpp movesort.cpp preeval.cpp evaluate.cpp search.cpp eleeye.cpp
g++ -g -w -O0 -Wall -DJIEQIMODE -oELEEYE_JIEQI.EXE ../base/pipe.cpp ucci.cpp pregen.cpp position.cpp genmoves.cpp hash.cpp book.cpp movesort.cpp preeval.cpp evaluate.cpp search.cpp eleeye.cpp


#编译3种代理服务器
gcc -g -o eleeye_server eleeye_server.c
gcc -g -DJIEQI -o eleeye_jieqiserver eleeye_server.c

# 先不搞皮卡鱼的吧
# gcc -g -DPIKAFISH -o pikafish_server eleeye_server.c

