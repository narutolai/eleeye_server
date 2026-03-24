/*
evaluate.cpp - Source Code for ElephantEye, Part XI

ElephantEye - a Chinese Chess Program (UCCI Engine)
Designed by Morning Yellow, Version: 3.3, Last Modified: Mar. 2012
Copyright (C) 2004-2012 www.xqbase.com

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "../base/base.h"
#include "pregen.h"
#include "position.h"
#include "preeval.h"

/* ElephantEye源程序使用的匈牙利记号约定：
 *
 * sq: 格子序号(整数，从0到255，参阅"pregen.cpp")
 * pc: 棋子序号(整数，从0到47，参阅"position.cpp")
 * pt: 棋子类型序号(整数，从0到6，参阅"position.cpp")
 * mv: 着法(整数，从0到65535，参阅"position.cpp")
 * sd: 走子方(整数，0代表红方，1代表黑方)
 * vl: 局面价值(整数，从"-MATE_VALUE"到"MATE_VALUE"，参阅"position.cpp")
 * (注：以上五个记号可与uc、dw等代表整数的记号配合使用)
 * pos: 局面(PositionStruct类型，参阅"position.h")
 * sms: 位行和位列的着法生成预置结构(参阅"pregen.h")
 * smv: 位行和位列的着法判断预置结构(参阅"pregen.h")
 */

// 偷懒评价的边界
const int EVAL_MARGIN1 = 160;
const int EVAL_MARGIN2 = 80;
const int EVAL_MARGIN3 = 40;
const int EVAL_MARGIN4 = 20;

// 本模块只涉及到"PositionStruct"中的"sdPlayer"、"ucpcSquares"、"ucsqPieces"和"wBitPiece"四个成员，故省略前面的"this->"

/* ElephantEye的局面评价内容共4有4部分
 * 1. 跟仕(士)有关的特殊棋型的评价，见"AdvisorShape()"；
 * 2. 车或炮牵制帅(将)或车的棋型的评价，见"StringHold()"；
 * 3. 车的灵活性的评价，见"RookMobility()"；
 * 4. 马受到阻碍的评价，见"KnightTrap()"。
 */

// 以下是第一部分，特殊棋型的评价

/* 仕(士)的形状对于局面评价，特别是判断空头炮、沉底炮等棋型有重大作用，为此ElephantEye给出四种形状：
 * 1. 帅(将)在原位，双仕(士)都在底线，编为1号，这种情况要判断空头炮和炮镇窝心马；
 * 2. 帅(将)在原位，双仕(士)从左边包围帅(将)，编为2号，这种情况要判断右边的沉底炮和车封右边的帅(将)门；
 * 3. 帅(将)在原位，双仕(士)从右边包围帅(将)，编为3号，这种情况要判断左边的沉底炮和车封左边的帅(将)门；
 * 4. 其他情况，包括帅(将)不在原位或缺仕(士)，都编号0。
 * 注：以“红下黑上”这个固定的棋盘方位来规定左右。
 */
const int WHITE_KING_BITFILE = 1 << (RANK_BOTTOM - RANK_TOP);
const int BLACK_KING_BITFILE = 1 << (RANK_TOP - RANK_TOP);
const int KING_BITRANK = 1 << (FILE_CENTER - FILE_LEFT);

const int SHAPE_NONE = 0;
const int SHAPE_CENTER = 1;
const int SHAPE_LEFT = 2;
const int SHAPE_RIGHT = 3;

int PositionStruct::AdvisorShape(void) const {
  int pcCannon, pcRook, sq, sqAdv1, sqAdv2, x, y, nShape;
  int vlWhitePenalty, vlBlackPenalty;
  SlideMaskStruct *lpsms;
  vlWhitePenalty = vlBlackPenalty = 0;
 
#ifdef JIEQIMODE
  if (((this->wBitPiece[0] & ADVISOR_BITPIECE) == ADVISOR_BITPIECE)
      ||((this->wBitPiece[2] & ADVISOR_BITPIECE) == ADVISOR_BITPIECE)) {
    if (this->ucsqPieces[SIDE_TAG(0) + KING_FROM] == 0xc7) {
      //加上揭棋的位置
      sqAdv1 = this->ucsqPieces[SIDE_TAG(0) + ADVISOR_FROM] + this->ucsqPieces[SIDE_TAG(0) + ADVISOR_FROM +32];
      sqAdv2 = this->ucsqPieces[SIDE_TAG(0) + ADVISOR_TO] +  this->ucsqPieces[SIDE_TAG(0) + ADVISOR_TO + 32];
#else
     if ((this->wBitPiece[0] & ADVISOR_BITPIECE) == ADVISOR_BITPIECE) {
    if (this->ucsqPieces[SIDE_TAG(0) + KING_FROM] == 0xc7) {
      sqAdv1 = this->ucsqPieces[SIDE_TAG(0) + ADVISOR_FROM];
      sqAdv2 = this->ucsqPieces[SIDE_TAG(0) + ADVISOR_TO];
#endif
      if (false) {
      } else if (sqAdv1 == 0xc6) { // 红方一个仕在左侧底线
        nShape = (sqAdv2 == 0xc8 ? SHAPE_CENTER : sqAdv2 == 0xb7 ? SHAPE_LEFT : SHAPE_NONE);
      } else if (sqAdv1 == 0xc8) { // 红方一个仕在右侧底线
        nShape = (sqAdv2 == 0xc6 ? SHAPE_CENTER : sqAdv2 == 0xb7 ? SHAPE_RIGHT : SHAPE_NONE);
      } else if (sqAdv1 == 0xb7) { // 红方一个仕在花心
        nShape = (sqAdv2 == 0xc6 ? SHAPE_LEFT : sqAdv2 == 0xc8 ? SHAPE_RIGHT : SHAPE_NONE);
      } else {
        nShape = SHAPE_NONE;
      }
      switch (nShape) {
      case SHAPE_NONE:
        break;
      case SHAPE_CENTER:
        for (pcCannon = SIDE_TAG(1) + CANNON_FROM; pcCannon <= SIDE_TAG(1) + CANNON_TO; pcCannon ++) {
          sq = this->ucsqPieces[pcCannon];
          if (sq != 0) {
            x = FILE_X(sq);
            if (x == FILE_CENTER) {
              y = RANK_Y(sq);
              lpsms = this->FileMaskPtr(x, y);
              if ((lpsms->wRookCap & WHITE_KING_BITFILE) != 0) {
                // 计算空头炮的威胁
                vlWhitePenalty += PreEvalEx.vlHollowThreat[RANK_FLIP(y)];
              } else if ((lpsms->wSuperCap & WHITE_KING_BITFILE) != 0 &&
                  (this->ucpcSquares[0xb7] == 21 || this->ucpcSquares[0xb7] == 22)) {
                // 计算炮镇窝心马的威胁
                vlWhitePenalty += PreEvalEx.vlCentralThreat[RANK_FLIP(y)];
              }
            }
          }
        }
        #ifdef JIEQIMODE
          //遍历暗子炮
          for (pcCannon = SIDE_TAG(1) + CANNON_FROM + 32; pcCannon <= SIDE_TAG(1) + CANNON_TO + 32; pcCannon ++) {
            sq = this->ucsqPieces[pcCannon];
            if (sq != 0) {
              x = FILE_X(sq);
              if (x == FILE_CENTER) {
                y = RANK_Y(sq);
                lpsms = this->FileMaskPtr(x, y);
                if ((lpsms->wRookCap & WHITE_KING_BITFILE) != 0) {
                  // 计算空头炮的威胁
                  vlWhitePenalty += PreEvalEx.vlHollowThreat[RANK_FLIP(y)];
                } else if ((lpsms->wSuperCap & WHITE_KING_BITFILE) != 0 &&
                    (this->ucpcSquares[0xb7] == 21 || this->ucpcSquares[0xb7] == 22)) {
                  // 计算炮镇窝心马的威胁
                  vlWhitePenalty += PreEvalEx.vlCentralThreat[RANK_FLIP(y)];
                }
              }
            }
          }
        #endif
        break;
      case SHAPE_LEFT:
      case SHAPE_RIGHT:
        for (pcCannon = SIDE_TAG(1) + CANNON_FROM; pcCannon <= SIDE_TAG(1) + CANNON_TO; pcCannon ++) {
          sq = this->ucsqPieces[pcCannon];
          if (sq != 0) {
            x = FILE_X(sq);
            y = RANK_Y(sq);
            if (x == FILE_CENTER) {
              if ((this->FileMaskPtr(x, y)->wSuperCap & WHITE_KING_BITFILE) != 0) {
                // 计算一般中炮的威胁，帅(将)门被对方控制的还有额外罚分
                vlWhitePenalty += (PreEvalEx.vlCentralThreat[RANK_FLIP(y)] >> 2) +
                    (this->Protected(1, nShape == SHAPE_LEFT ? 0xc8 : 0xc6) ? 20 : 0);
                // 如果车在底线保护帅(将)，则给予更大的罚分！
                for (pcRook = SIDE_TAG(0) + ROOK_FROM; pcRook <= SIDE_TAG(0) + ROOK_TO; pcRook ++) {
                  sq = this->ucsqPieces[pcRook];
                  if (sq != 0) {
                    y = RANK_Y(sq);
                    if (y == RANK_BOTTOM) {
                      x = FILE_X(sq);
                      if ((this->RankMaskPtr(x, y)->wRookCap & KING_BITRANK) != 0) {
                        vlWhitePenalty += 80;
                      }
                    }
                  }
                }
              }
            } else if (y == RANK_BOTTOM) {
              if ((this->RankMaskPtr(x, y)->wRookCap & KING_BITRANK) != 0) {
                // 计算沉底炮的威胁
                vlWhitePenalty += PreEvalEx.vlWhiteBottomThreat[x];
              }
            }
          }
        }
        
        #ifdef JIEQIMODE
          //遍历暗子炮
          for (pcCannon = SIDE_TAG(1) + CANNON_FROM + 32; pcCannon <= SIDE_TAG(1) + CANNON_TO + 32; pcCannon ++) {
            sq = this->ucsqPieces[pcCannon];
            if (sq != 0) {
              x = FILE_X(sq);
              if (x == FILE_CENTER) {
                y = RANK_Y(sq);
                lpsms = this->FileMaskPtr(x, y);
                if ((lpsms->wRookCap & WHITE_KING_BITFILE) != 0) {
                  // 计算空头炮的威胁
                  vlWhitePenalty += PreEvalEx.vlHollowThreat[RANK_FLIP(y)];
                } else if ((lpsms->wSuperCap & WHITE_KING_BITFILE) != 0 &&
                    (this->ucpcSquares[0xb7] == 21 || this->ucpcSquares[0xb7] == 22)) {
                  // 计算炮镇窝心马的威胁
                  vlWhitePenalty += PreEvalEx.vlCentralThreat[RANK_FLIP(y)];
                }
              }
            }
          }
        #endif
        
        break;
      default:
        break;
      }
    } else if (this->ucsqPieces[SIDE_TAG(0) + KING_FROM] == 0xb7) {
      // 有双仕(士)但花心被帅(将)占领，要罚分
      vlWhitePenalty += 20;
    }
  } else {
    if ((this->wBitPiece[1] & ROOK_BITPIECE) == ROOK_BITPIECE) {
      // 缺仕(士)怕双车，有罚分
      vlWhitePenalty += PreEvalEx.vlWhiteAdvisorLeakage;
    }
  }


#ifdef JIEQIMODE
     if (((this->wBitPiece[1] & ADVISOR_BITPIECE) == ADVISOR_BITPIECE)
      ||((this->wBitPiece[3] & ADVISOR_BITPIECE) == ADVISOR_BITPIECE)) {
    if (this->ucsqPieces[SIDE_TAG(1) + KING_FROM] == 0x37) {
      //加上揭棋的位置
      sqAdv1 = this->ucsqPieces[SIDE_TAG(1) + ADVISOR_FROM] + this->ucsqPieces[SIDE_TAG(1) + ADVISOR_FROM +32];
      sqAdv2 = this->ucsqPieces[SIDE_TAG(1) + ADVISOR_TO] +  this->ucsqPieces[SIDE_TAG(1) + ADVISOR_TO + 32];
#else
      if ((this->wBitPiece[1] & ADVISOR_BITPIECE) == ADVISOR_BITPIECE) {
    if (this->ucsqPieces[SIDE_TAG(1) + KING_FROM] == 0x37) {
      sqAdv1 = this->ucsqPieces[SIDE_TAG(1) + ADVISOR_FROM];
      sqAdv2 = this->ucsqPieces[SIDE_TAG(1) + ADVISOR_TO];
#endif
      if (false) {
      } else if (sqAdv1 == 0x36) { // 黑方一个士在左侧底线
        nShape = (sqAdv2 == 0x38 ? SHAPE_CENTER : sqAdv2 == 0x47 ? SHAPE_LEFT : SHAPE_NONE);
      } else if (sqAdv1 == 0x38) { // 黑方一个士在右侧底线
        nShape = (sqAdv2 == 0x36 ? SHAPE_CENTER : sqAdv2 == 0x47 ? SHAPE_RIGHT : SHAPE_NONE);
      } else if (sqAdv1 == 0x47) { // 黑方一个士在花心
        nShape = (sqAdv2 == 0x36 ? SHAPE_LEFT : sqAdv2 == 0x38 ? SHAPE_RIGHT : SHAPE_NONE);
      } else {
        nShape = SHAPE_NONE;
      }
      switch (nShape) {
      case SHAPE_NONE:
        break;
      case SHAPE_CENTER:
        for (pcCannon = SIDE_TAG(0) + CANNON_FROM; pcCannon <= SIDE_TAG(0) + CANNON_TO; pcCannon ++) {
          sq = this->ucsqPieces[pcCannon];
          if (sq != 0) {
            x = FILE_X(sq);
            if (x == FILE_CENTER) {
              y = RANK_Y(sq);
              lpsms = this->FileMaskPtr(x, y);
              if ((lpsms->wRookCap & BLACK_KING_BITFILE) != 0) {
                // 计算空头炮的威胁
                vlBlackPenalty += PreEvalEx.vlHollowThreat[y];
              } else if ((lpsms->wSuperCap & BLACK_KING_BITFILE) != 0 &&
                  (this->ucpcSquares[0x47] == 37 || this->ucpcSquares[0x47] == 38)) {
                // 计算炮镇窝心马的威胁
                vlBlackPenalty += PreEvalEx.vlCentralThreat[y];
              }
            }
          }
        }

        #ifdef JIEQIMODE
         for (pcCannon = SIDE_TAG(0) + CANNON_FROM +32; pcCannon <= SIDE_TAG(0) + CANNON_TO +32; pcCannon ++) {
          sq = this->ucsqPieces[pcCannon];
          if (sq != 0) {
            x = FILE_X(sq);
            if (x == FILE_CENTER) {
              y = RANK_Y(sq);
              lpsms = this->FileMaskPtr(x, y);
              if ((lpsms->wRookCap & BLACK_KING_BITFILE) != 0) {
                // 计算空头炮的威胁
                vlBlackPenalty += PreEvalEx.vlHollowThreat[y];
              } else if ((lpsms->wSuperCap & BLACK_KING_BITFILE) != 0 &&
                  (this->ucpcSquares[0x47] == 37 || this->ucpcSquares[0x47] == 38)) {
                // 计算炮镇窝心马的威胁
                vlBlackPenalty += PreEvalEx.vlCentralThreat[y];
              }
            }
          }
        }
        #endif

        break;
      case SHAPE_LEFT:
      case SHAPE_RIGHT:
        for (pcCannon = SIDE_TAG(0) + CANNON_FROM; pcCannon <= SIDE_TAG(0) + CANNON_TO; pcCannon ++) {
          sq = this->ucsqPieces[pcCannon];
          if (sq != 0) {
            x = FILE_X(sq);
            y = RANK_Y(sq);
            if (x == FILE_CENTER) {
              if ((this->FileMaskPtr(x, y)->wSuperCap & BLACK_KING_BITFILE) != 0) {
                // 计算一般中炮的威胁，帅(将)门被对方控制的还有额外罚分
                vlBlackPenalty += (PreEvalEx.vlCentralThreat[y] >> 2) +
                    (this->Protected(0, nShape == SHAPE_LEFT ? 0x38 : 0x36) ? 20 : 0);
                // 如果车在底线保护帅(将)，则给予更大的罚分！
                for (pcRook = SIDE_TAG(1) + ROOK_FROM; pcRook <= SIDE_TAG(1) + ROOK_TO; pcRook ++) {
                  sq = this->ucsqPieces[pcRook];
                  if (sq != 0) {
                    y = RANK_Y(sq);
                    if (y == RANK_TOP) {
                      x = FILE_X(sq);
                      if ((this->RankMaskPtr(x, y)->wRookCap & KING_BITRANK) != 0) {
                        vlBlackPenalty += 80;
                      }
                    }
                  }
                }
              }
            } else if (y == RANK_TOP) {
              if ((this->RankMaskPtr(x, y)->wRookCap & KING_BITRANK) != 0) {
                // 计算沉底炮的威胁
                vlBlackPenalty += PreEvalEx.vlBlackBottomThreat[x];
              }
            }
          }
        }
        #ifdef JIEQIMODE
         for (pcCannon = SIDE_TAG(0) + CANNON_FROM +32; pcCannon <= SIDE_TAG(0) + CANNON_TO +32; pcCannon ++) {
            sq = this->ucsqPieces[pcCannon];
            if (sq != 0) {
              x = FILE_X(sq);
              if (x == FILE_CENTER) {
                y = RANK_Y(sq);
                lpsms = this->FileMaskPtr(x, y);
                if ((lpsms->wRookCap & BLACK_KING_BITFILE) != 0) {
                  // 计算空头炮的威胁
                  vlBlackPenalty += PreEvalEx.vlHollowThreat[y];
                } else if ((lpsms->wSuperCap & BLACK_KING_BITFILE) != 0 &&
                    (this->ucpcSquares[0x47] == 37 || this->ucpcSquares[0x47] == 38)) {
                  // 计算炮镇窝心马的威胁
                  vlBlackPenalty += PreEvalEx.vlCentralThreat[y];
                }
              }
            }
         }
        #endif
        break;
      default:
        break;
      }
    } else if (this->ucsqPieces[SIDE_TAG(1) + KING_FROM] == 0x47) {
      // 有双仕(士)但花心被帅(将)占领，要罚分
      vlBlackPenalty += 20;
    }
  } else {
    if ((this->wBitPiece[0] & ROOK_BITPIECE) == ROOK_BITPIECE) {
      // 缺仕(士)怕双车，有罚分
      vlBlackPenalty += PreEvalEx.vlBlackAdvisorLeakage;
    }
  }
  return SIDE_VALUE(this->sdPlayer, vlBlackPenalty - vlWhitePenalty);
}

// 以上是第一部分，特殊棋型的评价

// 以下是第二部分，牵制的评价

// 常数表"cnValuableStringPieces"用判断牵制是否有价值
// 大于0的项是对于车来说的，牵制马和炮(被牵制)都有价值，大于1的项是对于炮来说只有牵制马才有价值
static const int cnValuableStringPieces[48] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0, 0
};

// "ccvlStringValueTab"是类似"KNIGHT_PIN_TAB"的常数表(参阅"pregen.h")，决定牵制价值
// 中间子和被牵制子的距离越近，牵制的价值就越大
static const char ccvlStringValueTab[512] = {
                               0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 12,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 16,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 20,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 24,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 28,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 32,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 36,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 40,  0,  0,  0,  0,  0,  0,  0,  0,
  12, 16, 20, 24, 28, 32, 36,  0, 36, 32, 28, 24, 20, 16, 12,  0,
   0,  0,  0,  0,  0,  0,  0, 40,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 36,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 32,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 28,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 24,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 20,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 16,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0, 12,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0
};

// 车或炮牵制帅(将)或车的棋型的评价
int PositionStruct::StringHold(void) const {
  int sd, i, j, nDir, sqSrc, sqDst, sqStr;
  int x, y, nSideTag, nOppSideTag;
  int vlString[2];
  SlideMoveStruct *lpsmv;

  for (sd = 0; sd < 2; sd ++) {
    vlString[sd] = 0;
    nSideTag = SIDE_TAG(sd);
    nOppSideTag = OPP_SIDE_TAG(sd);
    // 考查用车来牵制的情况
    for (i = ROOK_FROM; i <= ROOK_TO; i ++) {
      sqSrc = this->ucsqPieces[nSideTag + i];
      if (sqSrc != 0) {
        __ASSERT_SQUARE(sqSrc);
        // 考查牵制目标是帅(将)的情况
        sqDst = this->ucsqPieces[nOppSideTag + KING_FROM];
        if (sqDst != 0) {
          __ASSERT_SQUARE(sqDst);
          x = FILE_X(sqSrc);
          y = RANK_Y(sqSrc);
          if (x == FILE_X(sqDst)) {
            lpsmv = this->FileMovePtr(x, y);
            nDir = (sqSrc < sqDst ? 0 : 1);
            // 如果车用炮的吃法(炮用超级炮的着法)能吃到目标子"sqDst"，牵制就成立了，下同
            if (sqDst == lpsmv->ucCannonCap[nDir] + FILE_DISP(x)) {
              // 被牵制子"sqStr"是车(炮)本身能吃到的棋子，下同
              sqStr = lpsmv->ucRookCap[nDir] + FILE_DISP(x);
              __ASSERT_SQUARE(sqStr);
              // 被牵制子必须是对方的子，下同
              if ((this->ucpcSquares[sqStr] & nOppSideTag) != 0) {
                // 如果被牵制子是有价值的，而且被牵制子没有保护(被目标子保护不算)，那么牵制是有价值的，下同
                if (cnValuableStringPieces[this->ucpcSquares[sqStr]] > 0 &&
                    !this->Protected(OPP_SIDE(sd), sqStr, sqDst)) {
                  vlString[sd] += ccvlStringValueTab[sqDst - sqStr + 256];
                }
              }
            }
          } else if (y == RANK_Y(sqDst)) {
            lpsmv = this->RankMovePtr(x, y);
            nDir = (sqSrc < sqDst ? 0 : 1);
            if (sqDst == lpsmv->ucCannonCap[nDir] + RANK_DISP(y)) {
              sqStr = lpsmv->ucRookCap[nDir] + RANK_DISP(y);
              __ASSERT_SQUARE(sqStr);
              if ((this->ucpcSquares[sqStr] & nOppSideTag) != 0) {
                if (cnValuableStringPieces[this->ucpcSquares[sqStr]] > 0 &&
                    !this->Protected(OPP_SIDE(sd), sqStr, sqDst)) {
                  vlString[sd] += ccvlStringValueTab[sqDst - sqStr + 256];
                }
              }
            }
          }
        }
        // 考查牵制目标是车的情况
        for (j = ROOK_FROM; j <= ROOK_TO; j ++) {
          sqDst = this->ucsqPieces[nOppSideTag + j];
          if (sqDst != 0) {
            __ASSERT_SQUARE(sqDst);
            x = FILE_X(sqSrc);
            y = RANK_Y(sqSrc);
            if (x == FILE_X(sqDst)) {
              lpsmv = this->FileMovePtr(x, y);
              nDir = (sqSrc < sqDst ? 0 : 1);
              if (sqDst == lpsmv->ucCannonCap[nDir] + FILE_DISP(x)) {
                sqStr = lpsmv->ucRookCap[nDir] + FILE_DISP(x);
                __ASSERT_SQUARE(sqStr);
                if ((this->ucpcSquares[sqStr] & nOppSideTag) != 0) {
                  // 目标子是车，不同于帅(将)，要求车也没有保护时才有牵制价值，下同
                  if (cnValuableStringPieces[this->ucpcSquares[sqStr]] > 0 &&
                      !this->Protected(OPP_SIDE(sd), sqDst) && !this->Protected(OPP_SIDE(sd), sqStr, sqDst)) {
                    vlString[sd] += ccvlStringValueTab[sqDst - sqStr + 256];
                  }
                }
              }
            } else if (y == RANK_Y(sqDst)) {
              lpsmv = this->RankMovePtr(x, y);
              nDir = (sqSrc < sqDst ? 0 : 1);
              if (sqDst == lpsmv->ucCannonCap[nDir] + RANK_DISP(y)) {
                sqStr = lpsmv->ucRookCap[nDir] + RANK_DISP(y);
                __ASSERT_SQUARE(sqStr);
                if ((this->ucpcSquares[sqStr] & nOppSideTag) != 0) {
                  if (cnValuableStringPieces[this->ucpcSquares[sqStr]] > 0 &&
                      !this->Protected(OPP_SIDE(sd), sqDst) && !this->Protected(OPP_SIDE(sd), sqStr, sqDst)) {
                    vlString[sd] += ccvlStringValueTab[sqDst - sqStr + 256];
                  }
                }
              }
            }
          }
        }
      }
    }

    // 考查用炮来牵制的情况
    for (i = CANNON_FROM; i <= CANNON_TO; i ++) {
      sqSrc = this->ucsqPieces[nSideTag + i];
      if (sqSrc != 0) {
        __ASSERT_SQUARE(sqSrc);
        // 考查牵制目标是帅(将)的情况
        sqDst = this->ucsqPieces[nOppSideTag + KING_FROM];
        if (sqDst != 0) {
          __ASSERT_SQUARE(sqDst);
          x = FILE_X(sqSrc);
          y = RANK_Y(sqSrc);
          if (x == FILE_X(sqDst)) {
            lpsmv = this->FileMovePtr(x, y);
            nDir = (sqSrc < sqDst ? 0 : 1);
            if (sqDst == lpsmv->ucSuperCap[nDir] + FILE_DISP(x)) {
              sqStr = lpsmv->ucCannonCap[nDir] + FILE_DISP(x);
              __ASSERT_SQUARE(sqStr);
              if ((this->ucpcSquares[sqStr] & nOppSideTag) != 0) {
                if (cnValuableStringPieces[this->ucpcSquares[sqStr]] > 1 &&
                    !this->Protected(OPP_SIDE(sd), sqStr, sqDst)) {
                  vlString[sd] += ccvlStringValueTab[sqDst - sqStr + 256];
                }
              }
            }
          } else if (y == RANK_Y(sqDst)) {
            lpsmv = this->RankMovePtr(x, y);
            nDir = (sqSrc < sqDst ? 0 : 1);
            if (sqDst == lpsmv->ucSuperCap[nDir] + RANK_DISP(y)) {
              sqStr = lpsmv->ucCannonCap[nDir] + RANK_DISP(y);
              __ASSERT_SQUARE(sqStr);
              if ((this->ucpcSquares[sqStr] & nOppSideTag) != 0) {
                if (cnValuableStringPieces[this->ucpcSquares[sqStr]] > 1 &&
                    !this->Protected(OPP_SIDE(sd), sqStr, sqDst)) {
                  vlString[sd] += ccvlStringValueTab[sqDst - sqStr + 256];
                }
              }
            }
          }
        }
        // 考查牵制目标是车的情况
        for (j = ROOK_FROM; j <= ROOK_TO; j ++) {
          sqDst = this->ucsqPieces[nOppSideTag + j];
          if (sqDst != 0) {
            __ASSERT_SQUARE(sqDst);
            x = FILE_X(sqSrc);
            y = RANK_Y(sqSrc);
            if (x == FILE_X(sqDst)) {
              lpsmv = this->FileMovePtr(x, y);
              nDir = (sqSrc < sqDst ? 0 : 1);
              if (sqDst == lpsmv->ucSuperCap[nDir] + FILE_DISP(x)) {
                sqStr = lpsmv->ucCannonCap[nDir] + FILE_DISP(x);
                __ASSERT_SQUARE(sqStr);
                if ((this->ucpcSquares[sqStr] & nOppSideTag) != 0) {
                  if (cnValuableStringPieces[this->ucpcSquares[sqStr]] > 1 &&
                      !this->Protected(OPP_SIDE(sd), sqStr, sqDst)) {
                    vlString[sd] += ccvlStringValueTab[sqDst - sqStr + 256];
                  }
                }
              }
            } else if (y == RANK_Y(sqDst)) {
              lpsmv = this->RankMovePtr(x, y);
              nDir = (sqSrc < sqDst ? 0 : 1);
              if (sqDst == lpsmv->ucSuperCap[nDir] + RANK_DISP(y)) {
                sqStr = lpsmv->ucCannonCap[nDir] + RANK_DISP(y);
                __ASSERT_SQUARE(sqStr);
                if ((this->ucpcSquares[sqStr] & nOppSideTag) != 0) {
                  if (cnValuableStringPieces[this->ucpcSquares[sqStr]] > 1 &&
                      !this->Protected(OPP_SIDE(sd), sqStr, sqDst)) {
                    vlString[sd] += ccvlStringValueTab[sqDst - sqStr + 256];
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return SIDE_VALUE(this->sdPlayer, vlString[0] - vlString[1]);
}

// 以上是第二部分，牵制的评价

// 以下是第三部分，车的灵活性的评价

int PositionStruct::RookMobility(void) const {
  int sd, i, sqSrc, nSideTag, x, y;
  int vlRookMobility[2];
  for (sd = 0; sd < 2; sd ++) {
    vlRookMobility[sd] = 0;
    nSideTag = SIDE_TAG(sd);
    for (i = ROOK_FROM; i <= ROOK_TO; i ++) {
      sqSrc = this->ucsqPieces[nSideTag + i];
      if (sqSrc != 0) {
        __ASSERT_SQUARE(sqSrc);
        x = FILE_X(sqSrc);
        y = RANK_Y(sqSrc);
        vlRookMobility[sd] += PreEvalEx.cPopCnt16[this->RankMaskPtr(x, y)->wNonCap] +
            PreEvalEx.cPopCnt16[this->FileMaskPtr(x, y)->wNonCap];
      }
    }
    __ASSERT(vlRookMobility[sd] <= 34);
  }
  return SIDE_VALUE(this->sdPlayer, vlRookMobility[0] - vlRookMobility[1]) >> 1;
}

// 以上是第三部分，车的灵活性的评价

// 以下是第四部分，马受到阻碍的评价

// 常数表"cbcEdgeSquares"给定了不利于马的位置，处于棋盘边缘和两个花心位置的马都是坏马
static const bool cbcEdgeSquares[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

int PositionStruct::KnightTrap(void) const {
  int sd, i, sqSrc, sqDst, nSideTag, nMovable;
  uint8_t *lpucsqDst, *lpucsqPin;
  int vlKnightTraps[2];

  for (sd = 0; sd < 2; sd ++) {
    vlKnightTraps[sd] = 0;
    nSideTag = SIDE_TAG(sd);
    // 考虑马可以走的位置，走到棋盘边缘上，或者走到对方的控制格，都必须排除
    for (i = KNIGHT_FROM; i <= KNIGHT_TO; i ++) {
      sqSrc = this->ucsqPieces[nSideTag + i];
      if (sqSrc != 0) {
        __ASSERT_SQUARE(sqSrc);
        nMovable = 0;
        lpucsqDst = PreGen.ucsqKnightMoves[sqSrc];
        lpucsqPin = PreGen.ucsqKnightPins[sqSrc];
        sqDst = *lpucsqDst;
        while (sqDst != 0) {
          __ASSERT_SQUARE(sqDst);
          // 以下的判断区别于"genmoves.cpp"中的着法生成器，排除了走到棋盘边缘和走到对方控制格的着法
          if (!cbcEdgeSquares[sqDst] && this->ucpcSquares[sqDst] == 0 &&
              this->ucpcSquares[*lpucsqPin] == 0 && !this->Protected(OPP_SIDE(sd), sqDst)) {
            nMovable ++;
            if (nMovable > 1) {
              break;
            }
          }
          lpucsqDst ++;
          sqDst = *lpucsqDst;
          lpucsqPin ++;
        }
        // 没有好的着法的马给予10分罚分，只有一个好的着法的马给予5分罚分
        if (nMovable == 0) {
          vlKnightTraps[sd] += 10;
        } else if (nMovable == 1) {
          vlKnightTraps[sd] += 5;
        }
      }
      __ASSERT(vlKnightTraps[sd] <= 20);
    }
  }
  return SIDE_VALUE(this->sdPlayer, vlKnightTraps[1] - vlKnightTraps[0]);
}

// 以上是第四部分，马受到阻碍的评价

// 局面评价过程
int PositionStruct::Evaluate(int vlAlpha, int vlBeta) const {
  int vl;
  // 偷懒的局面评价函数分以下几个层次：

  // 1. 四级偷懒评价(彻底偷懒评价)，只包括子力平衡；
  vl = this->Material();
  if (vl + EVAL_MARGIN1 <= vlAlpha) {
    return vl + EVAL_MARGIN1;
  } else if (vl - EVAL_MARGIN1 >= vlBeta) {
    return vl - EVAL_MARGIN1;
  }

  // 2. 三级偷懒评价，包括特殊棋型；
  vl += this->AdvisorShape();
  if (vl + EVAL_MARGIN2 <= vlAlpha) {
    return vl + EVAL_MARGIN2;
  } else if (vl - EVAL_MARGIN2 >= vlBeta) {
    return vl - EVAL_MARGIN2;
  }

  // 3. 二级偷懒评价，包括牵制；
  vl += this->StringHold();
  if (vl + EVAL_MARGIN3 <= vlAlpha) {
    return vl + EVAL_MARGIN3;
  } else if (vl - EVAL_MARGIN3 >= vlBeta) {
    return vl - EVAL_MARGIN3;
  }

  // 4. 一级偷懒评价，包括车的灵活性；
  vl += this->RookMobility();
  if (vl + EVAL_MARGIN4 <= vlAlpha) {
    return vl + EVAL_MARGIN4;
  } else if (vl - EVAL_MARGIN4 >= vlBeta) {
    return vl - EVAL_MARGIN4;
  }

  // 5. 零级偷懒评价(完全评价)，包括马的阻碍。
  return vl + this->KnightTrap();
}
