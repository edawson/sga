///-----------------------------------------------
// Copyright 2010 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// ErrorCorrectProcess - Wrapper to perform error correction
// for a sequence work item
//
#include "ErrorCorrectProcess.h"

//
//
//
ErrorCorrectProcess::ErrorCorrectProcess(const OverlapAlgorithm* pOverlapper, 
                                         int minOverlap) : m_pOverlapper(pOverlapper), 
                                                           m_minOverlap(minOverlap)
{

}

//
ErrorCorrectProcess::~ErrorCorrectProcess()
{

}

//
ErrorCorrectResult ErrorCorrectProcess::process(const SequenceWorkItem& workItem)
{
    static const double p_error = 0.01f;
    OverlapResult overlap_result = m_pOverlapper->overlapRead(workItem.read, m_minOverlap, &m_blockList);
    // Convert the overlap block list into a multi-overlap 
    MultiOverlap mo = blockListToMultiOverlap(workItem, m_blockList);
    
    // Perform correction
    ErrorCorrectResult result;
    result.correctSequence = mo.calculateConsensusFromPartition(p_error);
    result.flag = ECF_CORRECTED;
    m_blockList.clear();
    return result;
}

//
MultiOverlap ErrorCorrectProcess::blockListToMultiOverlap(const SequenceWorkItem& item, OverlapBlockList& blockList)
{
    std::string read_idx = makeIdxString(-1);
    std::string read_seq = item.read.seq.toString();
    MultiOverlap out(read_idx, read_seq);

    for(OverlapBlockList::iterator iter = blockList.begin(); iter != blockList.end(); ++iter)
    {
        std::string overlap_string = iter->getOverlapString(read_seq);

        // Compute the endpoints of the overlap
        int s1 = read_seq.length() - iter->overlapLen;
        int e1 = s1 + iter->overlapLen - 1;
        SeqCoord sc1(s1, e1, read_seq.length());

        int s2 = 0; // The start of the second hit must be zero by definition of a prefix/suffix match
        int e2 = s2 + iter->overlapLen - 1;
        SeqCoord sc2(s2, e2, overlap_string.length());

        // The coordinates are always with respect to the read, so flip them if
        // we aligned to/from the reverse of the read
        if(iter->flags.isQueryRev())
            sc1.flip();
        if(iter->flags.isTargetRev())
            sc2.flip();

        bool isRC = false; // since we transformed the original sequence, they are never RC
        if(sc1.isContained())
            continue; // skip containments

        // Add an overlap for each member of the block
        for(int64_t i = iter->ranges.interval[0].lower; i <= iter->ranges.interval[0].upper; ++i)
        {
            Overlap o(read_idx, sc1, makeIdxString(i), sc2, isRC, -1);
            out.add(overlap_string, o);
        }
    }
    return out;
}

// make an id string from a read index
std::string ErrorCorrectProcess::makeIdxString(int64_t idx)
{
    std::stringstream ss;
    ss << idx;
    return ss.str();
}

//
//
//
ErrorCorrectPostProcess::ErrorCorrectPostProcess(std::ostream* pWriter) : m_pWriter(pWriter)
{

}

//
void ErrorCorrectPostProcess::process(const SequenceWorkItem& item, const ErrorCorrectResult& result)
{
    SeqRecord correctedRecord = item.read;
    correctedRecord.seq = result.correctSequence;
    correctedRecord.write(*m_pWriter);
    //m_pOverlapper->writeResultASQG(*m_pASQGWriter, item.read, result);
}