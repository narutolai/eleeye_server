/*
search.h/search.cpp - Source Code for ElephantEye, Part VIII

ElephantEye - a Chinese Chess Program (UCCI Engine)
Designed by Morning Yellow, Version: 3.32, Last Modified: May 2012
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

#ifndef CCHESS_A3800
#include <stdio.h>
#endif
#include "../base/base2.h"
#include "pregen.h"
#include "position.h"
#include "hash.h"
#ifndef CCHESS_A3800
#include "ucci.h"
#include "book.h"
#endif
#include "movesort.h"
#include "search.h"

const int IID_DEPTH = 2;       // �ڲ�������������
const int SMP_DEPTH = 6;       // �������������
const int UNCHANGED_DEPTH = 4; // δ�ı�����ŷ������

const int DROPDOWN_VALUE = 20;   // ���ķ�ֵ
const int RESIGN_VALUE = 300;    // ����ķ�ֵ
const int DRAW_OFFER_VALUE = 40; // ��͵ķ�ֵ

SearchStruct Search;

// ������Ϣ���Ƿ�װ��ģ���ڲ���
static struct
{
  int64_t llTime;                     // ��ʱ��
  bool bStop, bPonderStop;            // ��ֹ�źźͺ�̨˼����Ϊ����ֹ�ź�
  bool bPopPv, bPopCurrMove;          // �Ƿ����pv��currmove
  int nPopDepth, vlPopValue;          // �������Ⱥͷ�ֵ
  int nAllNodes, nMainNodes;          // �ܽ���������������Ľ����
  int nUnchanged;                     // δ�ı�����ŷ������
  uint16_t wmvPvLine[MAX_MOVE_NUM];   // ��Ҫ����·���ϵ��ŷ��б�
  uint16_t wmvKiller[LIMIT_DEPTH][2]; // ɱ���ŷ���
  MoveSortStruct MoveSort;            // �������ŷ�����
} Search2;

#ifndef CCHESS_A3800

void BuildPos(PositionStruct &pos, const UcciCommStruct &UcciComm)
{
  int i, mv;
  pos.FromFen(UcciComm.szFenStr);
  for (i = 0; i < UcciComm.nMoveNum; i++)
  {
    mv = COORD_MOVE(UcciComm.lpdwMovesCoord[i]);
    if (mv == 0)
    {
      break;
    }
    if (pos.LegalMove(mv) && pos.MakeMove(mv) && pos.LastMove().CptDrw > 0)
    {
      // ʼ����pos.nMoveNum��ӳû���ӵĲ���
      pos.SetIrrev();
    }
  }
}

#endif

// �ж�����
static bool Interrupt(void)
{
  if (Search.bIdle)
  {
    Idle();
  }
  if (Search.nGoMode == GO_MODE_NODES)
  {
    if (!Search.bPonder && Search2.nAllNodes > Search.nNodes * 4)
    {
      Search2.bStop = true;
      return true;
    }
  }
  else if (Search.nGoMode == GO_MODE_TIMER)
  {
    if (!Search.bPonder && (int)(GetTime() - Search2.llTime) > Search.nMaxTimer)
    {
      Search2.bStop = true;
      return true;
    }
  }
  if (Search.bBatch)
  {
    return false;
  }

#ifdef CCHESS_A3800
  return false;
#else
  UcciCommStruct UcciComm;
  PositionStruct posProbe;
  // �������������ģʽ����ô�ȵ���UCCI���ͳ������ж��Ƿ���ֹ
  switch (BusyLine(UcciComm, Search.bDebug))
  {
  case UCCI_COMM_ISREADY:
    // "isready"ָ��ʵ����û������
    printf("readyok\n");
    fflush(stdout);
    return false;
  case UCCI_COMM_PONDERHIT:
    // "ponderhit"ָ��������ʱ���ܣ����"SearchMain()"������Ϊ�Ѿ��������㹻��ʱ�䣬 ��ô������ֹ�ź�
    if (Search2.bPonderStop)
    {
      Search2.bStop = true;
      return true;
    }
    else
    {
      Search.bPonder = false;
      return false;
    }
  case UCCI_COMM_PONDERHIT_DRAW:
    // "ponderhit draw"ָ��������ʱ���ܣ���������ͱ�־
    Search.bDraw = true;
    if (Search2.bPonderStop)
    {
      Search2.bStop = true;
      return true;
    }
    else
    {
      Search.bPonder = false;
      return false;
    }
  case UCCI_COMM_STOP:
    // "stop"ָ�����ֹ�ź�
    Search2.bStop = true;
    return true;
  case UCCI_COMM_PROBE:
    // "probe"ָ�����Hash����Ϣ
    BuildPos(posProbe, UcciComm);
    PopHash(posProbe);
    return false;
  case UCCI_COMM_QUIT:
    // "quit"ָ����˳��ź�
    Search.bQuit = Search2.bStop = true;
    return true;
  default:
    return false;
  }
#endif
}

#ifndef CCHESS_A3800

// �����Ҫ����
static void PopPvLine(int nDepth = 0, int vl = 0)
{
  uint16_t *lpwmv;
  uint32_t dwMoveStr;
  // �����δ�ﵽ��Ҫ�������ȣ���ô��¼����Ⱥͷ�ֵ���Ժ������
  if (nDepth > 0 && !Search2.bPopPv && !Search.bDebug)
  {
    Search2.nPopDepth = nDepth;
    Search2.vlPopValue = vl;
    return;
  }
  // ���ʱ������������
  printf("info time %d nodes %d\n", (int)(GetTime() - Search2.llTime), Search2.nAllNodes);
  fflush(stdout);
  if (nDepth == 0)
  {
    // ��������������������������Ѿ����������ô���������
    if (Search2.nPopDepth == 0)
    {
      return;
    }
    // ��ȡ��ǰû���������Ⱥͷ�ֵ
    nDepth = Search2.nPopDepth;
    vl = Search2.vlPopValue;
  }
  else
  {
    // �ﵽ��Ҫ�������ȣ���ô�Ժ󲻱������
    Search2.nPopDepth = Search2.vlPopValue = 0;
  }
  printf("info depth %d score %d pv", nDepth, vl);
  lpwmv = Search2.wmvPvLine;
  while (*lpwmv != 0)
  {
    dwMoveStr = MOVE_COORD(*lpwmv);
    printf(" %.4s", (const char *)&dwMoveStr);
    lpwmv++;
  }
  printf("\n");
  fflush(stdout);
}

#endif

// �޺��ü�
static int HarmlessPruning(const PositionStruct &pos, int vlBeta)
{
  int vl, vlRep;

  // 1. ɱ�岽���ü���
  vl = pos.nDistance - MATE_VALUE;
  if (vl >= vlBeta)
  {
    return vl;
  }

  // 2. ����ü���
  if (pos.IsDraw())
  {
    return 0; // ��ȫ��������ﲻ��"pos.DrawValue()";
  }

  // 3. �ظ��ü���
  vlRep = pos.RepStatus();
  if (vlRep > 0)
  {
    return pos.RepValue(vlRep);
  }

  return -MATE_VALUE;
}

// �����;������ۺ���
inline int Evaluate(const PositionStruct &pos, int vlAlpha, int vlBeta)
{
  int vl;
  vl = Search.bKnowledge ? pos.Evaluate(vlAlpha, vlBeta) : pos.Material();
  return vl == pos.DrawValue() ? vl - 1 : vl;
}

// ��̬��������
/*
���ĺ���Ŀ���ǣ������ˮƽ��ЧӦ����Horizon Effect����
������������SearchPV���ﵽԤ����ȣ������ѵ��� 8 �㣩ʱ�����ֱ�ӵ����������������ܻ�������С�
���磬�� 8 ��ʱ��Ե��˶Է�һ�����������ֺܸߣ�����ʵ�� 9 ��Է����ܰ���Ի�ȥ��Ϊ�˱������֡�ֻ����ǰ���桱�Ĵ���
������Ҫ��Ҷ�ӽڵ����������Щ**�����ҵ��ŷ���������ӡ��⽫����ֱ�������á�������**��Quiet���������ٽ�������
*/
static int SearchQuiesc(PositionStruct &pos, int vlAlpha, int vlBeta)
{
  int vlBest, vl, mv;
  bool bInCheck;
  MoveSortStruct MoveSort;
  // ��̬�������̰������¼������裺
  Search2.nAllNodes++;

  // 1. �޺��ü���(�ظ�������)
  vl = HarmlessPruning(pos, vlBeta);
  if (vl > -MATE_VALUE)
  {
    return vl;
  }

#ifdef HASH_QUIESC
  // 3. �û��ü���
  vl = ProbeHashQ(pos, vlAlpha, vlBeta);
  if (Search.bUseHash && vl > -MATE_VALUE)
  {
    return vl;
  }
#endif

  // 4. �ﵽ������ȣ�ֱ�ӷ�������ֵ�� ��ֹ�ݹ�̫��³��������
  if (pos.nDistance == LIMIT_DEPTH)
  {
    return Evaluate(pos, vlAlpha, vlBeta);
  }
  __ASSERT(Search.pos.nDistance < LIMIT_DEPTH);

  // 5. ��ʼ����
  vlBest = -MATE_VALUE;
  bInCheck = (pos.LastMove().ChkChs > 0);

  // 6. ���ڱ������ľ��棬����ȫ���ŷ���
  if (bInCheck)
  {
    MoveSort.InitAll(pos);
  }
  else
  {
    // 7. ����δ�������ľ��棬�������ŷ�ǰ���ȳ��Կ���(��������)�����Ծ��������ۣ�
    vl = Evaluate(pos, vlAlpha, vlBeta);
    __ASSERT_BOUND(1 - WIN_VALUE, vl, WIN_VALUE - 1);
    __ASSERT(vl > vlBest);
    if (vl >= vlBeta)
    {
#ifdef HASH_QUIESC
      RecordHashQ(pos, vl, MATE_VALUE);
#endif
      // ����Ҳ����壨������״�����������Ѿ��� Beta ���ˡ�
      // ˵����ǰ�����Ѿ��㹻�ã����߶Է�֮ǰ�и��õ�ѡ�񣨲��������ߵ���һ������
      // û��Ҫ��ȥ�ѳ����ˣ�ֱ�ӽضϡ�
      return vl;
    }
    // ���� Alpha
    vlBest = vl;
    vlAlpha = MAX(vl, vlAlpha);

    // 8. ����δ�������ľ��棬���ɲ��������г����ŷ�(MVV(LVA)����)��
    MoveSort.InitQuiesc(pos);
  }

  // 9. ��Alpha-Beta�㷨������Щ�ŷ���
  /*
  ���ѭ���� SearchPV ���񣬵�������
��ֻ�����ղ����ɵ��ŷ���������ʱ�������ŷ���û������ʱֻ�ǳ����ŷ�����
  */
  while ((mv = MoveSort.NextQuiesc(bInCheck)) != 0)
  {
    __ASSERT(bInCheck || pos.ucpcSquares[DST(mv)] > 0);
    if (pos.MakeMove(mv)) // ����
    {
      // �ݹ���ã�ע�ⴰ�ڷ�ת (-vlBeta, -vlAlpha)
      vl = -SearchQuiesc(pos, -vlBeta, -vlAlpha);
      pos.UndoMakeMove(); // ����
      if (vl > vlBest)
      {
        if (vl >= vlBeta)
        {
#ifdef HASH_QUIESC
          if (vl > -WIN_VALUE && vl < WIN_VALUE)
          {
            RecordHashQ(pos, vl, MATE_VALUE);
          }
#endif
          return vl;
        }
        vlBest = vl;
        vlAlpha = MAX(vl, vlAlpha);
      }
    }
  }

  // 10. ���ط�ֵ��
  if (vlBest == -MATE_VALUE)
  {
    __ASSERT(pos.IsMate());
    return pos.nDistance - MATE_VALUE;
  }
  else
  {
#ifdef HASH_QUIESC
    if (vlBest > -WIN_VALUE && vlBest < WIN_VALUE)
    {
      RecordHashQ(pos, vlBest > vlAlpha ? vlBest : -MATE_VALUE, vlBest);
    }
#endif
    return vlBest;
  }
}

#ifndef CCHESS_A3800

// UCCI֧�� - ���Ҷ�ӽ��ľ�����Ϣ
void PopLeaf(PositionStruct &pos)
{
  int vl;
  Search2.nAllNodes = 0;
  vl = SearchQuiesc(pos, -MATE_VALUE, MATE_VALUE);
  printf("pophash lowerbound %d depth 0 upperbound %d depth 0\n", vl, vl);
  fflush(stdout);
}

#endif

const bool NO_NULL = true; // "SearchCut()"�Ĳ������Ƿ��ֹ���Ųü�

// �㴰����ȫ��������
static int SearchCut(int vlBeta, int nDepth, bool bNoNull = false)
{
  int nNewDepth, vlBest, vl;
  int mvHash, mv, mvEvade;
  MoveSortStruct MoveSort;
  // ��ȫ�������̰������¼������裺

  // 1. ��Ҷ�ӽ�㴦���þ�̬������
  if (nDepth <= 0)
  {
    __ASSERT(nDepth >= -NULL_DEPTH);
    return SearchQuiesc(Search.pos, vlBeta - 1, vlBeta);
  }
  Search2.nAllNodes++;

  // 2. �޺��ü���
  vl = HarmlessPruning(Search.pos, vlBeta);
  if (vl > -MATE_VALUE)
  {
    return vl;
  }

  // 3. �û��ü���
  vl = ProbeHash(Search.pos, vlBeta - 1, vlBeta, nDepth, bNoNull, mvHash);
  if (Search.bUseHash && vl > -MATE_VALUE)
  {
    return vl;
  }

  // 4. �ﵽ������ȣ�ֱ�ӷ�������ֵ��
  if (Search.pos.nDistance == LIMIT_DEPTH)
  {
    return Evaluate(Search.pos, vlBeta - 1, vlBeta);
  }
  __ASSERT(Search.pos.nDistance < LIMIT_DEPTH);

  // 5. �жϵ��ã�
  Search2.nMainNodes++;
  vlBest = -MATE_VALUE;
  if ((Search2.nMainNodes & Search.nCountMask) == 0 && Interrupt())
  {
    return vlBest;
  }

  // 6. ���Կ��Ųü���
  if (Search.bNullMove && !bNoNull && Search.pos.LastMove().ChkChs <= 0 && Search.pos.NullOkay())
  {
    Search.pos.NullMove();
    vl = -SearchCut(1 - vlBeta, nDepth - NULL_DEPTH - 1, NO_NULL);
    Search.pos.UndoNullMove();
    if (Search2.bStop)
    {
      return vlBest;
    }

    if (vl >= vlBeta)
    {
      if (Search.pos.NullSafe())
      {
        // a. ������Ųü��������飬��ô��¼�������Ϊ(NULL_DEPTH + 1)��
        RecordHash(Search.pos, HASH_BETA, vl, MAX(nDepth, NULL_DEPTH + 1), 0);
        return vl;
      }
      else if (SearchCut(vlBeta, nDepth - NULL_DEPTH, NO_NULL) >= vlBeta)
      {
        // b. ������Ųü������飬��ô��¼�������Ϊ(NULL_DEPTH)��
        RecordHash(Search.pos, HASH_BETA, vl, MAX(nDepth, NULL_DEPTH), 0);
        return vl;
      }
    }
  }

  // 7. ��ʼ����
  if (Search.pos.LastMove().ChkChs > 0)
  {
    // ����ǽ������棬��ô��������Ӧ���ŷ���
    mvEvade = MoveSort.InitEvade(Search.pos, mvHash, Search2.wmvKiller[Search.pos.nDistance]);
  }
  else
  {
    // ������ǽ������棬��ôʹ���������ŷ��б���
    MoveSort.InitFull(Search.pos, mvHash, Search2.wmvKiller[Search.pos.nDistance]);
    mvEvade = 0;
  }

  // 8. ����"MoveSortStruct::NextFull()"���̵��ŷ�˳����һ������
  while ((mv = MoveSort.NextFull(Search.pos)) != 0)
  {
    if (Search.pos.MakeMove(mv))
    {

      // 9. ����ѡ�������죻
      nNewDepth = (Search.pos.LastMove().ChkChs > 0 || mvEvade != 0 ? nDepth : nDepth - 1);

      // 10. �㴰��������
      vl = -SearchCut(1 - vlBeta, nNewDepth);
      Search.pos.UndoMakeMove();
      if (Search2.bStop)
      {
        return vlBest;
      }

      // 11. �ض��ж���
      if (vl > vlBest)
      {
        vlBest = vl;
        if (vl >= vlBeta)
        {
          RecordHash(Search.pos, HASH_BETA, vlBest, nDepth, mv);
          if (!MoveSort.GoodCap(Search.pos, mv))
          {
            SetBestMove(mv, nDepth, Search2.wmvKiller[Search.pos.nDistance]);
          }
          return vlBest;
        }
      }
    }
  }

  // 12. ���ضϴ�ʩ��
  if (vlBest == -MATE_VALUE)
  {
    __ASSERT(Search.pos.IsMate());
    return Search.pos.nDistance - MATE_VALUE;
  }
  else
  {
    RecordHash(Search.pos, HASH_ALPHA, vlBest, nDepth, mvEvade);
    return vlBest;
  }
}

// ������Ҫ����
static void AppendPvLine(uint16_t *lpwmvDst, uint16_t mv, const uint16_t *lpwmvSrc)
{
  *lpwmvDst = mv;
  lpwmvDst++;
  while (*lpwmvSrc != 0)
  {
    *lpwmvDst = *lpwmvSrc;
    lpwmvSrc++;
    lpwmvDst++;
  }
  *lpwmvDst = 0;
}

/* ��Ҫ������ȫ�������̣����㴰����ȫ���������������¼��㣺
 *
 * 1. �����ڲ���������������
 * 2. ��ʹ���и���Ӱ��Ĳü���
 * 3. Alpha-Beta�߽��ж����ӣ�
 * 4. PV���Ҫ��ȡ��Ҫ������
 * 5. ����PV��㴦������ŷ��������
 * ����һ�������ķ������� (vlAlpha, vlBeta) �ڣ�����ȫ��ȵ���������ͼ�ҵ���ǰ�������ʵ���ֺ�����ŷ���
 *  �������ͨ��ֻ���������ġ�����ࡱ·����Ҳ�������п��ܳ�Ϊ����ŷ���·�����ϱ����á�
 */
static int SearchPV(int vlAlpha, int vlBeta, int nDepth, uint16_t *lpwmvPvLine)
{
  int nNewDepth, nHashFlag, vlBest, vl;
  int mvBest, mvHash, mv, mvEvade;
  MoveSortStruct MoveSort;
  uint16_t wmvPvLine[LIMIT_DEPTH];
  // ��ȫ�������̰������¼������裺

  // 1. ��Ҷ�ӽ�㴦���þ�̬������
  *lpwmvPvLine = 0;
  if (nDepth <= 0)
  {
    __ASSERT(nDepth >= -NULL_DEPTH);
    return SearchQuiesc(Search.pos, vlAlpha, vlBeta);
    // ��ʣ��������� nDepth �ľ���<= 0��ʱ�����ٽ���ȫ����������ת�뾲̬������Quiescence Search����
    // ��̬����ֻ�������ӵȼ����ֶΣ�ֱ������ƽ�ȣ�Ȼ������������� Evaluate ��֡�
    // ����Ϊ�˷�ֹ��ˮƽ��ЧӦ��������Ϊ�˱��ⶪ�Ӷ���һ���ͳ����Ƴٶ��ӵ�ʱ�䣬������Ⱥľ�ʱ���о��棩��
  }
  Search2.nAllNodes++;

  // 2. �޺��ü�������Ƿ�������ظ����棨��������׽�ȣ�������ǣ�ֱ�ӷ��غ�����и��ķ��������������ѡ�
  /*
  ��������������֮ǰ�ǲ����ѹ�������ѹ�������㹻��>= nDepth����ֱ���ñ���ķ����������ٷѾ����ˡ�
  mvHash����������м�¼����˳��ѵ�ʱ������ŷ���Hash Move��ȡ��������������ʱ������
  */
  vl = HarmlessPruning(Search.pos, vlBeta);
  if (vl > -MATE_VALUE)
  {
    return vl;
  }

  // 3. �û��ü�����̬����Ҳ��ר�ŵ��û�����HashQ��������������֮ǰ�ѹ���ֱ���ý����
  vl = ProbeHash(Search.pos, vlAlpha, vlBeta, nDepth, NO_NULL, mvHash);
  if (Search.bUseHash && vl > -MATE_VALUE)
  {
    // ����PV��㲻�����û��ü������Բ��ᷢ��PV·���жϵ����
    return vl;
  }

  // 4. �ﵽ������ȣ�ֱ�ӷ�������ֵ��
  __ASSERT(Search.pos.nDistance > 0);
  if (Search.pos.nDistance == LIMIT_DEPTH)
  {
    return Evaluate(Search.pos, vlAlpha, vlBeta);
  }
  __ASSERT(Search.pos.nDistance < LIMIT_DEPTH);

  // 5. �жϵ��ã�
  Search2.nMainNodes++;
  vlBest = -MATE_VALUE;
  if ((Search2.nMainNodes & Search.nCountMask) == 0 && Interrupt())
  {
    return vlBest;
  }

  // 6. �ڲ���������������
  /*
    �����������ǰ��Ⱥ�����绹Ҫ�� 8 �㣩�������û�����û������ŷ���mvHash == 0�������ǾͲ�֪���Ĳ���á�
    ���⣺���Ϲ�ѣ�Alpha-Beta ��֦Ч�ʻ�ܵ͡�
    �Բߣ����ý�ǳ����ȣ����� nDepth / 2��������һ�顣��Ȼ�������ȷ�������ҳ�һ������ʲ����ġ�����ŷ�����Ȼ��������ŷ���Ϊָ������ȥ�����������
  */
  if (nDepth > IID_DEPTH && mvHash == 0)
  {
    __ASSERT(nDepth / 2 <= nDepth - IID_DEPTH);
    vl = SearchPV(vlAlpha, vlBeta, nDepth / 2, wmvPvLine);
    if (vl <= vlAlpha)
    {
      vl = SearchPV(-MATE_VALUE, vlBeta, nDepth / 2, wmvPvLine);
    }
    if (Search2.bStop)
    {
      return vlBest;
    }
    mvHash = wmvPvLine[0];
  }

  // 7. ��ʼ����
  mvBest = 0;
  nHashFlag = HASH_ALPHA;
  if (Search.pos.LastMove().ChkChs > 0)//������һ���ǽ���
  {
    // ����ǽ������棬��ô��������Ӧ���ŷ���
    mvEvade = MoveSort.InitEvade(Search.pos, mvHash, Search2.wmvKiller[Search.pos.nDistance]);
  }
  else
  {
    // ������ǽ������棬��ôʹ���������ŷ��б���
    MoveSort.InitFull(Search.pos, mvHash, Search2.wmvKiller[Search.pos.nDistance]);
    mvEvade = 0;
  }

  // 8. ����"MoveSortStruct::NextFull()"���̵��ŷ�˳����һ������
  while ((mv = MoveSort.NextFull(Search.pos)) != 0)
  {
    if (Search.pos.MakeMove(mv))
    {
      // 9. ����ѡ�������죻
      nNewDepth = (Search.pos.LastMove().ChkChs > 0 || mvEvade != 0 ? nDepth : nDepth - 1);

      // 10. ��Ҫ����������
      if (vlBest == -MATE_VALUE)
      {
        vl = -SearchPV(-vlBeta, -vlAlpha, nNewDepth, wmvPvLine);
      }
      else
      {
        vl = -SearchCut(-vlAlpha, nNewDepth);
        if (vl > vlAlpha && vl < vlBeta)
        {
          vl = -SearchPV(-vlBeta, -vlAlpha, nNewDepth, wmvPvLine);
        }
      }
      Search.pos.UndoMakeMove();
      if (Search2.bStop)
      {
        return vlBest;
      }

      // 11. Alpha-Beta�߽��ж���
      if (vl > vlBest)
      {
        vlBest = vl;
        if (vl >= vlBeta)
        {
          mvBest = mv;
          nHashFlag = HASH_BETA;
          break;
        }
        if (vl > vlAlpha)
        {
          vlAlpha = vl;
          mvBest = mv;
          nHashFlag = HASH_PV;
          AppendPvLine(lpwmvPvLine, mv, wmvPvLine);
        }
      }
    }
  }

  // 12. �����û�������ʷ����ɱ���ŷ�����
  if (vlBest == -MATE_VALUE)
  {
    __ASSERT(Search.pos.IsMate());
    return Search.pos.nDistance - MATE_VALUE;
  }
  else
  {
    RecordHash(Search.pos, nHashFlag, vlBest, nDepth, mvEvade == 0 ? mvBest : mvEvade);
    if (mvBest != 0 && !MoveSort.GoodCap(Search.pos, mvBest))
    {
      SetBestMove(mvBest, nDepth, Search2.wmvKiller[Search.pos.nDistance]);
    }
    return vlBest;
  }
}

/* ������������̣�����ȫ���������������¼��㣺
 *
 * 1. ʡ���޺��ü�(Ҳ����ȡ�û����ŷ�)��
 * 2. ��ʹ�ÿ��Ųü���
 * 3. ѡ��������ֻʹ�ý������죻
 * 4. ���˵���ֹ�ŷ���
 * 5. ����������ŷ�ʱҪ���ܶദ��(������¼��Ҫ��������������)��
 * 6. ��������ʷ����ɱ���ŷ�����
 * ���ĺ��������ǣ��ڵ�ǰ�����£��������п��ܵ��߷������õݹ�����������SearchPV �� SearchCut��������ÿһ���߷��ĺû���
 * �������ҳ�����ŷ���Best Move������Ҫ������Principal Variation, PV����
 */
static int SearchRoot(int nDepth)
{
  int nNewDepth, vlBest, vl, mv, nCurrMove;
#ifndef CCHESS_A3800
  uint32_t dwMoveStr;
#endif
  uint16_t wmvPvLine[LIMIT_DEPTH];
  // ������������̰������¼������裺

  // 1. ��ʼ��
  vlBest = -MATE_VALUE;         // ��ʼ��ѷ�����Ϊ�������ɱ��
  Search2.MoveSort.ResetRoot(); // ���ø��ڵ���߷���������׼����ͷ��ʼȡ�߷�

  // 2. ��һ����ÿ���ŷ�(Ҫ���˽�ֹ�ŷ�)
  nCurrMove = 0;
  while ((mv = Search2.MoveSort.NextRoot()) != 0)
  { // ȡ����һ���߷�
    if (Search.pos.MakeMove(mv))
    { // ������������ִ������߷�
#ifndef CCHESS_A3800
      if (Search2.bPopCurrMove || Search.bDebug)
      {
        dwMoveStr = MOVE_COORD(mv);
        nCurrMove++;
        printf("info currmove %.4s currmovenumber %d\n", (const char *)&dwMoveStr, nCurrMove);
        fflush(stdout); // ... ��ӡ��ǰ�����������߷���Ϣ (UCIЭ�� info currmove) ...
      }
#endif

      // 3. ����ѡ��������(ֻ���ǽ�������)
      /*�����ǰ�ⲽ���ǡ���������ChkChs > 0������ô�Է�����Ӧ��������ȽϽ��ȡ�
      Ϊ�˷�ֹ������Ȳ������¿�����ɱ�壬���ǲ�����ʣ����ȣ�nNewDepth = nDepth�����൱�����������һ����*/
      nNewDepth = (Search.pos.LastMove().ChkChs > 0 ? nDepth : nDepth - 1);

      // 4. ��Ҫ��������
      if (vlBest == -MATE_VALUE)
      {
        /*
             // ���A�������������ĵ�һ���Ϸ��߷�
              // ���Ǽ���������õģ�ͨ�����ڵ��߷��Ѿ��Ź����ˣ���������ȫ���ڣ�Full Window����������������
              // ���ڷ�Χ��(-MATE, +MATE)
        */
        vl = -SearchPV(-MATE_VALUE, MATE_VALUE, nNewDepth, wmvPvLine);
        __ASSERT(vl > vlBest);
      }
      else
      {
        // ���B���������߷�
        // ���� PVS ���裬�����߷�����ʲ����һ���߷��á�
        // �������ü�С���ڣ�Null Window / Zero Window�����п�����̽������
        // ���ڷ�Χ��(-vlBest - 1, -vlBest)���� (-Alpha - 1, -Alpha)
        vl = -SearchCut(-vlBest, nNewDepth); // �㴰��������ֻ��֤���Ƿ�� Alpha �á����ٶȼ��쵫������׼��

        // �����̽�����Ľ����ʾ����߷���Ȼ�ȵ�ǰ��ѻ��� (vl > vlBest)
        // ˵���ղŵļ�����ˣ�����߷��Ǹ�Ǳ���ɡ�
        // ������ȫ�������½���һ������������Re-search�������������ʵ������
        if (vl > vlBest)
        {                                                             // ���ﲻ��Ҫ" && vl < MATE_VALUE"��
          vl = -SearchPV(-MATE_VALUE, -vlBest, nNewDepth, wmvPvLine); // ��������������׼ȷ������
        }
      }

      Search.pos.UndoMakeMove(); // �ָ����̣�׼������һ���߷�
      if (Search2.bStop)         // ����յ�ָֹͣ���ʱ�䵽�ˣ�����������
      {
        return vlBest;
      }

      // 5. Alpha-Beta�߽��ж�("vlBest"������"SearchPV()"�е�"vlAlpha")
      if (vl > vlBest)
      {

        // 6. �����������һ�ŷ�����ô"δ�ı�����ŷ�"�ļ�������1����������
        Search2.nUnchanged = (vlBest == -MATE_VALUE ? Search2.nUnchanged + 1 : 0);
        vlBest = vl;

        // 7. ����������ŷ�ʱ��¼��Ҫ����
        AppendPvLine(Search2.wmvPvLine, mv, wmvPvLine);
#ifndef CCHESS_A3800
        PopPvLine(nDepth, vl);
#endif

        // 8. ���Ҫ��������ԣ���AlphaֵҪ���������������������ɱ��ʱ�����������
        if (vlBest > -WIN_VALUE && vlBest < WIN_VALUE)
        {
          vlBest += (Search.rc4Random.NextLong() & Search.nRandomMask) -
                    (Search.rc4Random.NextLong() & Search.nRandomMask);
          vlBest = (vlBest == Search.pos.DrawValue() ? vlBest - 1 : vlBest);
        }

        // 9. ���¸�����ŷ��б�
        Search2.MoveSort.UpdateRoot(mv);
      }
    }
  }
  return vlBest;
}

// Ψһ�ŷ�������ElephantEye�������ϵ�һ����ɫ�������ж�����ĳ����Ƚ��е������Ƿ��ҵ���Ψһ�ŷ���
// ��ԭ���ǰ��ҵ�������ŷ���ɽ�ֹ�ŷ���Ȼ����(-WIN_VALUE, 1 - WIN_VALUE)�Ĵ�������������
// ����ͳ��߽���˵�������ŷ�������ɱ��
static bool SearchUnique(int vlBeta, int nDepth)
{
  int vl, mv;
  Search2.MoveSort.ResetRoot(ROOT_UNIQUE);
  // ������һ���ŷ�
  while ((mv = Search2.MoveSort.NextRoot()) != 0)
  {
    if (Search.pos.MakeMove(mv))
    {
      vl = -SearchCut(1 - vlBeta, Search.pos.LastMove().ChkChs > 0 ? nDepth : nDepth - 1);
      Search.pos.UndoMakeMove();
      if (Search2.bStop || vl >= vlBeta)
      {
        return false;
      }
    }
  }
  return true;
}

// ����������
void SearchMain(int nDepth)
{
  int i, vl, vlLast, nDraw;
  int nCurrTimer, nLimitTimer, nLimitNodes;
  bool bUnique;
#ifndef CCHESS_A3800
  int nBookMoves;
  uint32_t dwMoveStr;
  BookStruct bks[MAX_GEN_MOVES];
#endif
  // ���������̰������¼������裺

  // 1. ����������ֱ�ӷ���
  if (Search.pos.IsDraw() || Search.pos.RepStatus(3) > 0)
  {
#ifndef CCHESS_A3800
    printf("nobestmove\n");
    fflush(stdout);
#endif
    return;
  }

#ifndef CCHESS_A3800
  // 2. �ӿ��ֿ��������ŷ�
  if (Search.bUseBook)
  {
    // a. ��ȡ���ֿ��е������߷�
    nBookMoves = GetBookMoves(Search.pos, Search.szBookFile, bks);
    if (nBookMoves > 0)
    {
      vl = 0;
      for (i = 0; i < nBookMoves; i++)
      {
        vl += bks[i].wvl;
        dwMoveStr = MOVE_COORD(bks[i].wmv);
        printf("info depth 0 score %d pv %.4s\n", bks[i].wvl, (const char *)&dwMoveStr);
        fflush(stdout);
      }
      // b. ����Ȩ�����ѡ��һ���߷�
      vl = Search.rc4Random.NextLong() % (uint32_t)vl;
      for (i = 0; i < nBookMoves; i++)
      {
        vl -= bks[i].wvl;
        if (vl < 0)
        {
          break;
        }
      }
      __ASSERT(vl < 0);
      __ASSERT(i < nBookMoves);
      // c. ������ֿ��е��ŷ�����ѭ�����棬��ô��������ŷ�
      Search.pos.MakeMove(bks[i].wmv);
      if (Search.pos.RepStatus(3) == 0)
      {
        dwMoveStr = MOVE_COORD(bks[i].wmv);
        printf("bestmove %.4s", (const char *)&dwMoveStr);
        // d. ������̨˼�����ŷ�(���ֿ��е�һ����Ȩ�����ĺ����ŷ�)
        nBookMoves = GetBookMoves(Search.pos, Search.szBookFile, bks);
        Search.pos.UndoMakeMove();
        if (nBookMoves > 0)
        {
          dwMoveStr = MOVE_COORD(bks[0].wmv);
          printf(" ponder %.4s", (const char *)&dwMoveStr);
        }
        printf("\n");
        fflush(stdout);
        return;
      }
      Search.pos.UndoMakeMove();
    }
  }
#endif

  // 3. ������Ϊ���򷵻ؾ�̬����ֵ
  if (nDepth == 0)
  {
#ifndef CCHESS_A3800
    printf("info depth 0 score %d\n", SearchQuiesc(Search.pos, -MATE_VALUE, MATE_VALUE));
    fflush(stdout);
    printf("nobestmove\n");
    fflush(stdout);
#endif
    return;
  }

  // 4. ���ɸ�����ÿ���ŷ�
  Search2.MoveSort.InitRoot(Search.pos, Search.nBanMoves, Search.wmvBanList);

  // 5. ��ʼ��ʱ��ͼ�����
  Search2.bStop = Search2.bPonderStop = Search2.bPopPv = Search2.bPopCurrMove = false;
  Search2.nPopDepth = Search2.vlPopValue = 0;
  Search2.nAllNodes = Search2.nMainNodes = Search2.nUnchanged = 0;
  Search2.wmvPvLine[0] = 0;
  ClearKiller(Search2.wmvKiller);
  ClearHistory();
  ClearHash();
  // ���� ClearHash() ��Ҫ����һ��ʱ�䣬���Լ�ʱ�����Ժ�ʼ�ȽϺ���
  Search2.llTime = GetTime();
  vlLast = 0;
  // �������10�غ������ŷ�����ô����������ͣ��Ժ�ÿ��8�غ����һ��
  nDraw = -Search.pos.LastMove().CptDrw;
  if (nDraw > 5 && ((nDraw - 4) / 2) % 8 == 0)
  {
    Search.bDraw = true;
  }
  bUnique = false;
  nCurrTimer = 0;

  // 6. ��������������
  for (i = 1; i <= nDepth; i++)
  {
    // ��Ҫ�����Ҫ����ʱ����һ��"info depth n"�ǲ������
#ifndef CCHESS_A3800
    if (Search2.bPopPv || Search.bDebug)
    {
      printf("info depth %d\n", i);
      fflush(stdout);
    }

    // 7. ����������ʱ��������Ƿ���Ҫ�����Ҫ�����͵�ǰ˼�����ŷ�
    Search2.bPopPv = (nCurrTimer > 300);
    Search2.bPopCurrMove = (nCurrTimer > 3000);
#endif

    // 8. ���������
    vl = SearchRoot(i);
    if (Search2.bStop)
    {
      if (vl > -MATE_VALUE)
      {
        vlLast = vl; // ������vlLast�������ж������Ͷ����������Ҫ�������һ��ֵ
      }
      break; // û����������"vl"�ǿɿ�ֵ
    }

    nCurrTimer = (int)(GetTime() - Search2.llTime);
    // 9. �������ʱ�䳬���ʵ�ʱ�ޣ�����ֹ����
    if (Search.nGoMode == GO_MODE_TIMER)
    {
      // a. ���û��ʹ�ÿ��Ųü�����ô�ʵ�ʱ�޼���(��Ϊ��֦���Ӽӱ���)
      nLimitTimer = (Search.bNullMove ? Search.nProperTimer : Search.nProperTimer / 2);
      // b. �����ǰ����ֵû�����ǰһ��ܶ࣬��ô�ʵ�ʱ�޼���
      nLimitTimer = (vl + DROPDOWN_VALUE >= vlLast ? nLimitTimer / 2 : nLimitTimer);
      // c. �������ŷ��������û�б仯����ô�ʵ�ʱ�޼���
      nLimitTimer = (Search2.nUnchanged >= UNCHANGED_DEPTH ? nLimitTimer / 2 : nLimitTimer);
      if (nCurrTimer > nLimitTimer)
      {
        if (Search.bPonder)
        {
          Search2.bPonderStop = true; // ������ں�̨˼��ģʽ����ôֻ���ں�̨˼�����к�������ֹ������
        }
        else
        {
          vlLast = vl;
          break; // �����Ƿ�������"vlLast"���Ѹ���
        }
      }
    }
    else if (Search.nGoMode == GO_MODE_NODES)
    {
      // nLimitNodes�ļ��㷽����nLimitTimer��һ����
      nLimitNodes = (Search.bNullMove ? Search.nNodes : Search.nNodes / 2);
      nLimitNodes = (vl + DROPDOWN_VALUE >= vlLast ? nLimitNodes / 2 : nLimitNodes);
      nLimitNodes = (Search2.nUnchanged >= UNCHANGED_DEPTH ? nLimitNodes / 2 : nLimitNodes);
      // GO_MODE_NODES���ǲ��ӳ���̨˼��ʱ���
      if (Search2.nAllNodes > nLimitNodes)
      {
        vlLast = vl;
        break;
      }
    }
    vlLast = vl;

    // 10. ������ɱ������ֹ����
    if (vlLast > WIN_VALUE || vlLast < -WIN_VALUE)
    {
      break;
    }

    // 11. ��Ψһ�ŷ�������ֹ����
    if (SearchUnique(1 - WIN_VALUE, i))
    {
      bUnique = true;
      break;
    }
  }

#ifdef CCHESS_A3800
  Search.mvResult = Search2.wmvPvLine[0];
#else
  // 12. �������ŷ��������Ӧ��(��Ϊ��̨˼���Ĳ²��ŷ�)
  if (Search2.wmvPvLine[0] != 0)
  {
    PopPvLine();
    dwMoveStr = MOVE_COORD(Search2.wmvPvLine[0]);
    printf("bestmove %.4s", (const char *)&dwMoveStr);
    if (Search2.wmvPvLine[1] != 0)
    {
      dwMoveStr = MOVE_COORD(Search2.wmvPvLine[1]);
      printf(" ponder %.4s", (const char *)&dwMoveStr);
    }

    // 13. �ж��Ƿ��������ͣ����Ǿ���Ψһ�ŷ�����Ĳ��ʺ���������(��Ϊ������Ȳ���)
    if (!bUnique)
    {
      if (vlLast > -WIN_VALUE && vlLast < -RESIGN_VALUE)
      {
        printf(" resign");
      }
      else if (Search.bDraw && !Search.pos.NullSafe() && vlLast < DRAW_OFFER_VALUE * 2)
      {
        printf(" draw");
      }
    }
  }
  else
  {
    printf("nobestmove");
  }
  printf("\n");
  fflush(stdout);
#endif
}
