//-----------------------------------------------
// Copyright 2009 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// OverlapBlock - Data structures holding
// the result of the alignment of a sequence read
// to a BWT
// 
#include "OverlapBlock.h"
#include "BWTAlgorithms.h"

// 
OverlapBlock::OverlapBlock(BWTIntervalPair r, int ol, 
                           int nd, const AlignFlags& af, 
                           const SearchHistoryVector& backHist) : ranges(r), 
                                                                  overlapLen(ol), 
                                                                  numDiff(nd),
                                                                  flags(af),
                                                                  isEliminated(false),
                                                                  backHistory(backHist)
{
    backHistory.normalize(af.isQueryComp());
}

// Return a pointer to the BWT that should be used to extend the block
// this is the opposite BWT that was used in the backwards search
const BWT* OverlapBlock::getExtensionBWT(const BWT* pBWT, const BWT* pRevBWT) const
{
    if(!flags.isTargetRev())
        return pRevBWT;
    else
        return pBWT;
}

// 
AlphaCount64 OverlapBlock::getCanonicalExtCount(const BWT* pBWT, const BWT* pRevBWT) const
{
    AlphaCount64 out = BWTAlgorithms::getExtCount(ranges.interval[1], getExtensionBWT(pBWT, pRevBWT));
    if(flags.isQueryComp())
        out.complement();
    return out;
}

// Returns 0 if the BWT used for the overlap step was the forward BWT
int OverlapBlock::getCanonicalIntervalIndex() const
{
    if(!flags.isTargetRev())
        return 0;
    else
        return 1;
}

// Get the string corresponding to the overlap block. This is the string found
// during the backwards search
std::string OverlapBlock::getOverlapString(const std::string& original)
{
    std::string transformed = backHistory.transform(original, flags.isQueryRev());
    // If the query was reversed, we take the first overlapLen (the search
    // was from the front of the sequence) otherwise we take the last overlapLen
    if(flags.isQueryRev())
        return transformed.substr(0, overlapLen);
    else
        return transformed.substr(transformed.length() - overlapLen);
}

// Get the full string corresponding to this overlapblock using the forward history
std::string OverlapBlock::getFullString(const std::string& original)
{
    std::string str = getOverlapString(original);
    std::string history = forwardHistory.getBaseString();

/*
    std::cout << "OVERLAP: " << str << "\n";
    std::cout << "HIST: " << history << "\n";
    std::cout << "QREV: " << flags.isQueryRev() << "\n";
    std::cout << "RC: " << flags.isReverseComplement() << "\n";
    std::cout << "QC: " << flags.isQueryComp() << "\n";
*/
    if(!flags.isQueryRev())
    {
        str.append(history);
    }
    else
    {
        history = reverse(history);
        history.append(str);
        str.swap(history);
    }

    if(flags.isReverseComplement())
        str = reverseComplement(str);
    return str;
}


EdgeDir OverlapBlock::getEdgeDir() const
{
    if(flags.isQueryRev())
        return ED_ANTISENSE;
    else
        return ED_SENSE;
}

//
Overlap OverlapBlock::toOverlap(const std::string queryID, const std::string targetID, int queryLen, int targetLen) const
{
    // Compute the sequence coordinates
    int s1 = queryLen - overlapLen;
    int e1 = s1 + overlapLen - 1;
    SeqCoord sc1(s1, e1, queryLen);

    int s2 = 0; // The start of the second hit must be zero by definition of a prefix/suffix match
    int e2 = s2 + overlapLen - 1;
    SeqCoord sc2(s2, e2, targetLen);

    // The coordinates are always with respect to the read, so flip them if
    // we aligned to/from the reverse of the read
    if(flags.isQueryRev())
        sc1.flip();
    if(flags.isTargetRev())
        sc2.flip();

    bool isRC = flags.isReverseComplement();

    Overlap o(queryID, sc1, targetID, sc2, isRC, numDiff);
    return o;
}

//
void printList(OverlapBlockList* pList)
{
    for(OverlapBlockList::iterator i = pList->begin(); i != pList->end(); ++i)
    {
        std::cout << "Block: " << *i << "\n";
    }
}

//
void removeSubMaximalBlocks(OverlapBlockList* pList)
{
    // This algorithm removes any sub-maximal OverlapBlocks from pList
    // The list is sorted by the left coordinate and iterated through
    // if two adjacent blocks overlap they are split into maximal contiguous regions
    // with resolveOverlap. The resulting list is merged back into pList. This process
    // is repeated until each block in pList is a unique range
    // The bookkeeping in the intersecting case could be more efficient 
    // but the vast vast majority of the cases will not have overlapping 
    // blocks.
    pList->sort(OverlapBlock::sortIntervalLeft);
    OverlapBlockList::iterator iter = pList->begin();
    OverlapBlockList::iterator last = pList->end();
    --last;

    while(iter != pList->end())
    {
        OverlapBlockList::iterator next = iter;
        ++next;

        if(next == pList->end())
            break;

        // Check if iter and next overlap
        if(Interval::isIntersecting(iter->ranges.interval[0].lower, iter->ranges.interval[0].upper, 
                                    next->ranges.interval[0].lower, next->ranges.interval[0].upper))
        {
            OverlapBlockList resolvedList = resolveOverlap(*iter, *next);
            
            // Merge the new elements in and start back from the beginning of the list
            pList->erase(iter);
            pList->erase(next);
            pList->merge(resolvedList, OverlapBlock::sortIntervalLeft);
            iter = pList->begin();

            //std::cout << "After splice: \n";
            //printList(pList);
        }
        else
        {
            ++iter;
        }
    }
}

// In rare cases, the overlap blocks may represent sub-maximal overlaps between reads
// we need to distinguish these cases and remove the sub-optimal hits. This
// function splits two overlapping OverlapBlocks into up to three distinct
// blocks, keeping the maximal (longest) overlap at each stage.
OverlapBlockList resolveOverlap(const OverlapBlock& A, const OverlapBlock& B)
{
    OverlapBlockList outList;

    // Check if A and B have the same overlap length, if so they must be 
    // identical blocks (resulting from different seeds) and we can remove one
    if(A.overlapLen == B.overlapLen)
    {
        if(A.ranges.interval[0].lower == B.ranges.interval[0].lower &&
           A.ranges.interval[0].upper == B.ranges.interval[0].upper)
        {
            outList.push_back(A);
            return outList;
        }
        else
        {
            std::cerr << "Error in resolveOverlap: Overlap blocks with same length do not "
            "the have same coordinates\n";
            assert(false);
        }    
    }
    // A and B must have different overlap lengths
    assert(A.overlapLen != B.overlapLen);

    // Determine which of A and B have a higher overlap
    const OverlapBlock* pHigher;
    const OverlapBlock* pLower;
    if(A.overlapLen > B.overlapLen)
    {
        pHigher = &A;
        pLower = &B;
    }
    else
    {
        pHigher = &B;
        pLower = &A;
    }

    // Complicated logic follows
    // We always want the entirity of the block with the longer
    // overlap so it is added to outList unmodified
    outList.push_back(*pHigher);

    // The lower block can be split into up to two pieces:
    // Case 1:
    //     Lower  ------ 
    //     Higher    ------
    //     Result ---
    //
    // Case 2:
    //     Lower  -----------
    //     Higher    ------
    //     Result ---      --
    //
    // Case 3:
    //     Lower  ------
    //     Higher ------
    //     Result (empty set)

    // It is unclear whether case 2 can happen in reality but we handle it 
    // here anyway. Further complicating matters is that the BWTIntervalPair
    // keeps track of both the BWT coordinates for the backwards search
    // and forward search and we must take care to ensure that both intervals
    // are updated and the mapping between them is sane. 
    //
    // Since the ordering of reads within the two intervals
    // is equal, by symmetrically performing the same operation on both intervals 
    // we preserve the mapping from the forward interval to the reverse. For instance 
    // if we contract the forward interval from [0 2] to [0 1] we must also contract the
    // reverse interval from [37 39] to [37 38].

    // Potentially split the lower block into two blocks
    OverlapBlock split = *pLower;

    // Left-hand split
    if(pLower->ranges.interval[0].lower < pHigher->ranges.interval[0].lower)
    {
        split.ranges.interval[0].lower = pLower->ranges.interval[0].lower;
        split.ranges.interval[0].upper = pHigher->ranges.interval[0].lower - 1; // inclusive
        
        // Calculate the new size of the interval and apply it to the reverse interval
        int64_t diff = split.ranges.interval[0].upper - split.ranges.interval[0].lower;

        split.ranges.interval[1].lower = pLower->ranges.interval[1].lower;
        split.ranges.interval[1].upper = split.ranges.interval[1].lower + diff;

        assert(split.ranges.interval[0].size() == split.ranges.interval[1].size());
        assert(split.ranges.interval[0].isValid());
        assert(split.ranges.interval[1].isValid());
        outList.push_back(split);
    }

    // Right-hand split
    if(pLower->ranges.interval[0].upper > pHigher->ranges.interval[0].upper)
    {
        split.ranges.interval[0].lower = pHigher->ranges.interval[0].upper + 1; // inclusive
        split.ranges.interval[0].upper = pLower->ranges.interval[0].upper;
        
        // Calculate the new size of the interval and apply it to the reverse interval
        int64_t diff = split.ranges.interval[0].upper - split.ranges.interval[0].lower;

        split.ranges.interval[1].upper = pLower->ranges.interval[1].upper;
        split.ranges.interval[1].lower = split.ranges.interval[1].upper - diff; 

        assert(split.ranges.interval[0].size() == split.ranges.interval[1].size());
        assert(split.ranges.interval[0].isValid());
        assert(split.ranges.interval[1].isValid());
        outList.push_back(split);
    }

    if(outList.size() == 3)
    {
        WARN_ONCE("Overlap block was split into 3 segments");
    }

    // Sort the outlist by left coordinate
    outList.sort(OverlapBlock::sortIntervalLeft);
    return outList;
}

// Partition the overlap block list into two lists, 
// one for the containment overlaps and one for the proper overlaps
void partitionBlockList(int readLen, OverlapBlockList* pCompleteList, 
                        OverlapBlockList* pOverlapList, 
                        OverlapBlockList* pContainList)
{
    OverlapBlockList::iterator iter = pCompleteList->begin();
    while(iter != pCompleteList->end())
    {
        if(iter->overlapLen == readLen)
            pContainList->splice(pContainList->end(), *pCompleteList, iter++);
        else
            pOverlapList->splice(pOverlapList->end(), *pCompleteList, iter++);
    }
}

// 
MultiOverlap blockListToMultiOverlap(const SeqRecord& record, OverlapBlockList& blockList)
{
    std::string read_idx = record.id;
    std::string read_seq = record.seq.toString();
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
std::string makeIdxString(int64_t idx)
{
    std::stringstream ss;
    ss << idx;
    return ss.str();
}

