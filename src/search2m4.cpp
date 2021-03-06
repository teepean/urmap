#include "myutils.h"
#include "ufindex.h"
#include "state2.h"
#include "seqinfo.h"
#include "omplock.h"
#include "sort.h"

#define PINTEST		0

#if PINTEST
uint32 GetTrueDBPos(SeqInfo *Query, const UFIndex &UFI);
bool SamePos(uint32 Pos1, uint32 Pos2);
#endif

void State2::Search4(SeqInfo *Query_Fwd, SeqInfo *Query_Rev)
	{
	m_UD_Fwd.InitPE(Query_Fwd);
	m_UD_Rev.InitPE(Query_Rev);

#if PINTEST
	uint32 TruePosf = GetTrueDBPos(Query_Fwd, *m_UD_Fwd.m_UFI);
	uint32 TruePosr = GetTrueDBPos(Query_Rev, *m_UD_Rev.m_UFI);
	Log("\n");
	Log("1>%s\n", Query_Fwd->m_Label);
	Log("2>%s\n", Query_Rev->m_Label);
#endif

	m_BestPairScore = 0;
	m_SecondBestPairScore = 0;
	m_BestPairIndex = 0;
	m_SecondPairIndex = UINT_MAX;

	unsigned QLf = m_UD_Fwd.m_Query->m_L;
	unsigned QLr = m_UD_Rev.m_Query->m_L;
	unsigned QL2 = (QLf + QLr)/2;
	unsigned Sizef = 2*QLf;
	unsigned Sizer = 2*QLr;

	if (Sizef > m_B1Sizef)
		{
		Sizef += 64;
		m_B1_QPosVecf.Alloc(Sizef);
		m_B1_PlusVecf.Alloc(Sizef);
		m_B1_DBPosVecf.Alloc(Sizef);
		m_B1Sizef = Sizef;
		}

	if (Sizer > m_B1Sizer)
		{
		Sizer += 64;
		m_B1_QPosVecr.Alloc(Sizer);
		m_B1_PlusVecr.Alloc(Sizer);
		m_B1_DBPosVecr.Alloc(Sizer);
		m_B1Sizer = Sizer;
		}

	m_TermPairScorePhase1 = int(QLf) + int(QLr) + 5*State1::MISMATCH_SCORE;//TODO:PARAM

	unsigned kf = UINT_MAX;
	unsigned kr = UINT_MAX;
	uint32 *B1_QPosVecf = m_B1_QPosVecf.Data;
	uint32 *B1_QPosVecr = m_B1_QPosVecr.Data;
	bool *B1_PlusVecf = m_B1_PlusVecf.Data;
	bool *B1_PlusVecr = m_B1_PlusVecr.Data;
	uint32 *B1_DBPosVecf = m_B1_DBPosVecf.Data;
	uint32 *B1_DBPosVecr = m_B1_DBPosVecr.Data;
	unsigned NB1f = 0;
	unsigned NB1r = 0;

	{
	uint32 QPosf;
	uint32 QPosr;
	bool Plusf;
	bool Plusr;
	uint32 DBPosf;
	uint32 DBPosr;
	kf = m_UD_Fwd.GetFirstBoth1Seed(QPosf, Plusf, DBPosf);
	kr = m_UD_Rev.GetFirstBoth1Seed(QPosr, Plusr, DBPosr);
	do
		{
		if (kf != UINT_MAX)
			{
			B1_QPosVecf[NB1f] = QPosf;
			B1_PlusVecf[NB1f] = Plusf;
			B1_DBPosVecf[NB1f] = DBPosf;
			++NB1f;
			for (unsigned i = 0; i < NB1r; ++i)
				{
				uint32 DBPosr = B1_DBPosVecr[i];
				int64 TL = abs(int64(DBPosf) - int64(DBPosr)) + int64(QL2);
				if (AcceptTemplateLength(TL))
					{
					uint32 QPosr = B1_QPosVecr[i];
					bool Done = ExtendBoth1Pair4(QPosf, DBPosf, Plusf,
					  QPosr, DBPosr);
#if PINTEST
					{
					bool Truef = SamePos(DBPosf, TruePosf);
					bool Truer = SamePos(DBPosr, TruePosr);
					Log("  (a) truef %c, truer %c, done %c\n",
					  tof(Truef), tof(Truer), tof(Done));
					}
#endif
					if (Done)
						return;
					}
				}
			}
		if (kr != UINT_MAX)
			{
			B1_QPosVecr[NB1r] = QPosr;
			B1_PlusVecr[NB1r] = Plusr;
			B1_DBPosVecr[NB1r] = DBPosr;
			++NB1r;
			for (unsigned i = 0; i < NB1f; ++i)
				{
				uint32 DBPosf = B1_DBPosVecf[i];
				int64 TL = abs(int64(DBPosf) - int64(DBPosr)) + int64(QL2);
				if (AcceptTemplateLength(TL))
					{
					uint32 QPosf = B1_QPosVecf[i];
					bool Done = ExtendBoth1Pair4(QPosf, DBPosf, !Plusr,
					  QPosr, DBPosr);
#if PINTEST
					{
					bool Truef = SamePos(DBPosf, TruePosf);
					bool Truer = SamePos(DBPosr, TruePosr);
					Log("  (b) truef %c, truer %c, done %c\n",
					  tof(Truef), tof(Truer), tof(Done));
					}
#endif
					if (Done)
						return;
					}
				}
			}
		if (kf != UINT_MAX)
			kf = m_UD_Fwd.GetNextBoth1Seed(kf, QPosf, Plusf, DBPosf);
		if (kr != UINT_MAX)
			kr = m_UD_Rev.GetNextBoth1Seed(kr, QPosr, Plusr, DBPosr);
		}
	while (kf != UINT_MAX || kr != UINT_MAX);
	}

	for (unsigned i = 0; i < NB1f; ++i)
		{
		uint32 QPos = B1_QPosVecf[i];
		uint32 DBPos = B1_DBPosVecf[i];
		bool Plus = B1_PlusVecf[i];
		m_UD_Fwd.ExtendPen(QPos, DBPos, Plus);
		}

	for (unsigned i = 0; i < NB1r; ++i)
		{
		uint32 QPos = B1_QPosVecr[i];
		uint32 DBPos = B1_DBPosVecr[i];
		bool Plus = B1_PlusVecr[i];
		m_UD_Rev.ExtendPen(QPos, DBPos, Plus);
		}

	int TermScore_Fwd = (QLf*9)/10;
	int TermScore_Rev = (QLr*9)/10;
	if (m_UD_Fwd.m_BestScore >= TermScore_Fwd &&
	  m_UD_Rev.m_BestScore >= TermScore_Rev)
		{
		uint32 DBPosf = m_UD_Fwd.m_TopHit->m_DBStartPos;
		uint32 DBPosr = m_UD_Rev.m_TopHit->m_DBStartPos;
		int64 TL = abs(int64(DBPosf) - int64(DBPosr)) + int64(QL2);
		if (AcceptTemplateLength(TL))
			{
			m_UD_Fwd.m_Mapq = 40;
			m_UD_Rev.m_Mapq = 40;
			return;
			}
		}

	m_UD_Fwd.SearchPE_Pending(kf);
	m_UD_Rev.SearchPE_Pending(kr);
	FindPairs();
	const unsigned N = SIZE(m_Paired_TotalScoreVec);
	if (N == 0)
		{
		ScanPair();
		FindPairs();
		}
	AdjustTopHitsAndMapqs();
	}

bool State2::ExtendBoth1Pair4(uint32 QPosf, uint32 DBPosf, bool Plusf,
  uint32 QPosr, uint32 DBPosr)
	{
	int FwdScore = m_UD_Fwd.ExtendPen(QPosf, DBPosf, Plusf);
	if (FwdScore <= 0)
		return false;

	int RevScore = m_UD_Rev.ExtendPen(QPosr, DBPosr, !Plusf);
	if (RevScore <= 0)
		return false;

	int SumScore = FwdScore + RevScore;
	if (SumScore < m_TermPairScorePhase1)
		return false;

	m_UD_Fwd.m_Mapq = 40;
	m_UD_Rev.m_Mapq = 40;

	return true;
	}
