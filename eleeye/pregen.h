/*
pregen.h/pregen.cpp - Source Code for ElephantEye, Part II

ElephantEye - a Chinese Chess Program (UCCI Engine)
Designed by Morning Yellow, Version: 3.0, Last Modified: Nov. 2007
Copyright (C) 2004-2007 www.elephantbase.net

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
#include "../base/rc4prng.h"

#ifndef PREGEN_H
#define PREGEN_H

#define __ASSERT_PIECE(pc) __ASSERT((pc) >= 16 && (pc) <= 79)
#define __ASSERT_SQUARE(sq) __ASSERT(IN_BOARD(sq))
#define __ASSERT_BITRANK(w) __ASSERT((w) < (1 << 9))
#define __ASSERT_BITFILE(w) __ASSERT((w) < (1 << 10))

const int RANK_TOP = 3;
const int RANK_BOTTOM = 12;
const int FILE_LEFT = 3;
const int FILE_CENTER = 7;
const int FILE_RIGHT = 11;

extern const bool cbcInBoard[256];    // ���������
extern const bool cbcInFort[256];     // �ǳ������
extern const bool cbcCanPromote[256]; // ���������
extern const int8_t ccLegalSpanTab[512];   // �����ŷ���ȱ�
extern const int8_t ccKnightPinTab[512];   // ���ȱ�

inline bool IN_BOARD(int sq) {
  return cbcInBoard[sq];
}

inline bool IN_FORT(int sq) {
  return cbcInFort[sq];
}

inline bool CAN_PROMOTE(int sq) {
  return cbcCanPromote[sq];
}

inline int8_t LEGAL_SPAN_TAB(int nDisp) {
  return ccLegalSpanTab[nDisp];
}

inline int8_t KNIGHT_PIN_TAB(int nDisp) {
  return ccKnightPinTab[nDisp];
}

inline int RANK_Y(int sq) {
  return sq >> 4;
}

inline int FILE_X(int sq) {
  return sq & 15;
}

inline int COORD_XY(int x, int y) {
  return x + (y << 4);
}

inline int SQUARE_FLIP(int sq) {
  return 254 - sq;
}

inline int FILE_FLIP(int x) {
  return 14 - x;
}

inline int RANK_FLIP(int y) {
  return 15 - y;
}

inline int OPP_SIDE(int sd) {
  return 1 - sd;
}

inline int SQUARE_FORWARD(int sq, int sd) {
  return sq - 16 + (sd << 5);
}

inline int SQUARE_BACKWARD(int sq, int sd) {
  return sq + 16 - (sd << 5);
}

inline bool KING_SPAN(int sqSrc, int sqDst) {
  return LEGAL_SPAN_TAB(sqDst - sqSrc + 256) == 1;
}

inline bool ADVISOR_SPAN(int sqSrc, int sqDst) {
  return LEGAL_SPAN_TAB(sqDst - sqSrc + 256) == 2;
}

inline bool BISHOP_SPAN(int sqSrc, int sqDst) {
  return LEGAL_SPAN_TAB(sqDst - sqSrc + 256) == 3;
}

inline int BISHOP_PIN(int sqSrc, int sqDst) {
  return (sqSrc + sqDst) >> 1;
}

inline int KNIGHT_PIN(int sqSrc, int sqDst) {
  return sqSrc + KNIGHT_PIN_TAB(sqDst - sqSrc + 256);
}

inline bool WHITE_HALF(int sq) {
  return (sq & 0x80) != 0;
}

inline bool BLACK_HALF(int sq) {
  return (sq & 0x80) == 0;
}

inline bool HOME_HALF(int sq, int sd) {
  return (sq & 0x80) != (sd << 7);
}

inline bool AWAY_HALF(int sq, int sd) {
  return (sq & 0x80) == (sd << 7);
}

inline bool SAME_HALF(int sqSrc, int sqDst) {
  return ((sqSrc ^ sqDst) & 0x80) == 0;
}

inline bool DIFF_HALF(int sqSrc, int sqDst) {
  return ((sqSrc ^ sqDst) & 0x80) != 0;
}

inline int RANK_DISP(int y) {
  return y << 4;
}

inline int FILE_DISP(int x) {
  return x;
}

// ������λ�С��͡�λ�С����ɳ����ŷ���Ԥ�ýṹ
struct SlideMoveStruct {
  uint8_t ucNonCap[2];    // ���������ߵ������һ��/��Сһ��
  uint8_t ucRookCap[2];   // ���������ߵ������һ��/��Сһ��
  uint8_t ucCannonCap[2]; // �ڳ������ߵ������һ��/��Сһ��
  uint8_t ucSuperCap[2];  // ������(�����ӳ���)���ߵ������һ��/��Сһ��
}; // smv

// ������λ�С��͡�λ�С��жϳ����ŷ������Ե�Ԥ�ýṹ
struct SlideMaskStruct {
  uint16_t wNonCap, wRookCap, wCannonCap, wSuperCap;
}; // sms

struct ZobristStruct {
  uint32_t dwKey, dwLock0, dwLock1;
  void InitZero(void) {
    dwKey = dwLock0 = dwLock1 = 0;
  }
  void InitRC4(RC4Struct &rc4) {
    dwKey = rc4.NextLong();
    dwLock0 = rc4.NextLong();
    dwLock1 = rc4.NextLong();
  }
  void Xor(const ZobristStruct &zobr) {
    dwKey ^= zobr.dwKey;
    dwLock0 ^= zobr.dwLock0;
    dwLock1 ^= zobr.dwLock1;
  }
  void Xor(const ZobristStruct &zobr1, const ZobristStruct &zobr2) {
    dwKey ^= zobr1.dwKey ^ zobr2.dwKey;
    dwLock0 ^= zobr1.dwLock0 ^ zobr2.dwLock0;
    dwLock1 ^= zobr1.dwLock1 ^ zobr2.dwLock1;
  }
}; // zobr

extern struct PreGenStruct {
  // Zobrist��ֵ������Zobrist��ֵ��ZobristУ����������
  ZobristStruct zobrPlayer;
  ZobristStruct zobrTable[14][256];

  uint16_t wBitRankMask[256]; // ÿ�����ӵ�λ�е�����λ
  uint16_t wBitFileMask[256]; // ÿ�����ӵ�λ�е�����λ

  /* ������λ�С��͡�λ�С����ɳ����ŷ����жϳ����ŷ������Ե�Ԥ������
   *
   * ��λ�С��͡�λ�С�������ElephantEye�ĺ��ļ������������������ڵ��ŷ����ɡ������жϺ;��������
   * �Գ�ʼ����췽�ұߵ����ڸ��е��ж�Ϊ�������ȱ���֪�����еġ�λ�С�����"1010000101b"��
   * ElephantEye������Ԥ�����飬��"...MoveTab"��"...MaskTab"���÷��ֱ��ǣ�
   * һ�����Ҫ֪��������ǰ���ӵ�Ŀ���(��ʼ����2��Ŀ�����9)����ôϣ���������֪��������ӣ�
   * ����Ԥ������һ������"FileMoveTab_CannonCap[10][1024]"��ʹ��"FileMoveTab_CannonCap[2][1010000101b] == 9"�Ϳ����ˡ�
   * �������Ҫ�жϸ����ܷ�Ե�Ŀ���(ͬ������ʼ����2��Ŀ�����9Ϊ��)����ô��Ҫ֪��Ŀ����λ�У���"0000000001b"��
   * ����ֻҪ��"...MoveTab"�ĸ����ԡ�����λ������ʽ���¼�������"...MaskTab"�Ϳ����ˣ��á��롱�������ж��ܷ�Ե�Ŀ���
   * ����ͨ��һ��"...MaskTab"��Ԫ������������λ���ж��ܷ�Ե�ͬ�л�ͬ�е�ĳ������ʱ��ֻ��Ҫ��һ���жϾͿ����ˡ�
   */
  SlideMoveStruct smvRankMoveTab[9][512];   // 36,864 �ֽ�
  SlideMoveStruct smvFileMoveTab[10][1024]; // 81,920 �ֽ�
  SlideMaskStruct smsRankMaskTab[9][512];   // 36,864 �ֽ�
  SlideMaskStruct smsFileMaskTab[10][1024]; // 81,920 �ֽ�
                                            // ����:  237,568 �ֽ�

  /* ��������(���ʺ��á�λ�С��͡�λ�С�)���ŷ�Ԥ��������
   *
   * �ⲿ�����������������ϵġ��ŷ�Ԥ���ɡ����飬���Ը���ĳ�����ӵ���ʼ��ֱ�Ӳ����飬�õ����е�Ŀ���
   * ʹ������ʱ�����Ը�����ʼ����ȷ��һ��ָ��"g_...Moves[Square]"�����ָ��ָ��һϵ��Ŀ�����0������
   * Ϊ�˶����ַ������[256][n]��n����4�ı��������ұ������n(��Ϊ��������˽�����ʶ��0)���������ۺ��������顣
   */
  uint8_t ucsqKingMoves[256][8];
  uint8_t ucsqAdvisorMoves[256][8];
  uint8_t ucsqBishopMoves[256][8];
  uint8_t ucsqBishopPins[256][4];
  uint8_t ucsqKnightMoves[256][12];
  uint8_t ucsqKnightPins[256][8];
  uint8_t ucsqPawnMoves[2][256][4];
} PreGen;

// ����Ԥ���۽ṹ
extern struct PreEvalStruct {
  bool bPromotion;
  int vlAdvanced;
  uint8_t ucvlWhitePieces[7][256];
  uint8_t ucvlBlackPieces[7][256];
} PreEval;

void PreGenInit(void);

#endif
