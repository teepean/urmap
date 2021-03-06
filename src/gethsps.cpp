#include "myutils.h"
#include "hspfinder.h"
#include "diagbox.h"
#include "alnheuristics.h"
#include <algorithm>

#define	TRACE	0

unsigned HSPFinder::GetGlobalHSPs(unsigned MinLength, float MinFractId,
  bool StaggerOk, float &HSPFractId)
	{
	IncCounter(GetGlobalHSPs);
	const byte *A = m_SA->m_Seq;
	const byte *B = m_SB->m_Seq;

	const unsigned LA = m_SA->m_L;
	const unsigned LB = m_SB->m_L;

	float X = m_AH->XDropGlobalHSP;
	UngappedBlast(X, StaggerOk, MinLength, m_AH->MinGlobalHSPScore);
	Chain();
	StartTimer(GetHSPs2);

	unsigned TotalLength = 0;
	unsigned TotalSameCount = 0;

	unsigned N = m_ChainedHSPCount;
#if TRACE
	Log("\n");
	Log("After chain, %u HSPs:\n", m_ChainedHSPCount);
#endif
	for (unsigned i = 0; i < m_ChainedHSPCount; ++i)
		{
		const HSPData &HSP = *m_ChainedHSPs[i];
#if TRACE
		HSP.LogMe();
#endif
		if (HSP.Leni != HSP.Lenj)
			{
			Warning("HSPFinder::GetHSPs, bad HSP");
			HSP.LogMe();

			m_UngappedHSPCount = 0;
			m_ChainedHSPCount = 0;
			EndTimer(GetHSPs2);
			return 0;
			}

		TotalLength += HSP.GetLength();
		TotalSameCount += GetHSPIdCount(HSP);
		}

	HSPFractId = TotalLength == 0 ? 0.0f :
	  float(TotalSameCount)/float(TotalLength);
	EndTimer(GetHSPs2);
	return m_ChainedHSPCount;
	}
