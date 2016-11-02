#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <ctime>
#include <string>
#include <climits>
#include <queue>

#include <seqan/align.h>
#include <seqan/find.h>

#include "edlib.h"

#include "SimpleEditDistance.h"

using namespace std;

int readFastaSequences(const char* path, vector< vector<unsigned char> >* seqs,
                       unsigned char* letterIdx, char* idxToLetter, bool* inAlphabet, int &alphabetLength);

void printAlignment(const unsigned char* query, const int queryLength,
                    const unsigned char* target, const int targetLength,
                    const unsigned char* alignment, const int alignmentLength,
                    const int position, const int modeCode, const char* idxToLetter);

// For debugging
void printSeq(const vector<unsigned char> &seq) {
    for (int i = 0; i < seq.size(); i++)
        printf("%d ", seq[i]);
    printf("\n");
}

int main(int argc, char * const argv[]) {
    typedef seqan::String<unsigned char> TSequence;                 // sequence type
    typedef seqan::Align<TSequence, seqan::ArrayGaps> TAlign;     // align type
    
    //----------------------------- PARSE COMMAND LINE ------------------------//
    // If true, there will be no output.
    bool silent = false;
    // Alignment mode.
    char mode[16] = "NW";
    // How many best sequences (those with smallest score) do we want.
    // If 0, then we want them all.
    int numBestSeqs = 0;
    bool findAlignment = false;
    bool findStartLocations = false;
    int option;
    int kArg = -1;
    // If true, simple implementation of edit distance algorithm is used instead of edlib.
    // This is for testing purposes.
    bool useSimple = false;
    // If "STD" or "EXT", cigar string will be printed. if "NICE" nice representation
    // of alignment will be printed.
    char alignmentFormat[16] = "NICE";

    bool invalidOption = false;
    while ((option = getopt(argc, argv, "m:n:k:f:splt")) >= 0) {
        switch (option) {
        case 'm': strcpy(mode, optarg); break;
        case 'n': numBestSeqs = atoi(optarg); break;
        case 'k': kArg = atoi(optarg); break;
        case 'f': strcpy(alignmentFormat, optarg); break;
        case 's': silent = true; break;
        case 'p': findAlignment = true; break;
        case 'l': findStartLocations = true; break;
        case 't': useSimple = true; break;
        default: invalidOption = true;
        }
    }
    if (optind + 2 != argc || invalidOption) {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: aligner [options...] <queries.fasta> <target.fasta>\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "\t-s  If specified, there will be no score or alignment output (silent mode).\n");
        fprintf(stderr, "\t-m HW|NW|SHW  Alignment mode that will be used. [default: NW]\n");
        fprintf(stderr, "\t-n N  Score will be calculated only for N best sequences (best = with smallest score)."
                " If N = 0 then all sequences will be calculated."
                " Specifying small N can make total calculation much faster. [default: 0]\n");
        fprintf(stderr, "\t-k K  Sequences with score > K will be discarded."
                " Smaller k, faster calculation.\n");
        fprintf(stderr, "\t-t  If specified, simple algorithm is used instead of edlib. To be used for testing.\n");
        fprintf(stderr, "\t-p  If specified, alignment path will be found and printed. "
                "This may significantly slow down the calculation.\n");
        fprintf(stderr, "\t-l  If specified, start locations will be found and printed. "
                "Each start location corresponds to one end location. This may somewhat slow down "
                "the calculation, but is still faster then finding alignment path and does not consume "
                "any extra memory.\n");
        fprintf(stderr, "\t-f NICE|CIG_STD|CIG_EXT  Format that will be used to print alignment path,"
                " can be used only with -p. NICE will give visually attractive format, CIG_STD will "
                " give standard cigar format and CIG_EXT will give extended cigar format. [default: NICE]\n");
        return 1;
    }
    //-------------------------------------------------------------------------//

    if (strcmp(alignmentFormat, "NICE") && strcmp(alignmentFormat, "CIG_STD") &&
        strcmp(alignmentFormat, "CIG_EXT")) {
        printf("Invalid alignment path format (-f)!\n");
        return 1;
    }

    int modeCode;
    if (!strcmp(mode, "SHW"))
        modeCode = EDLIB_MODE_SHW;
    else if (!strcmp(mode, "HW"))
        modeCode = EDLIB_MODE_HW;
    else if (!strcmp(mode, "NW"))
        modeCode = EDLIB_MODE_NW;
    else {
        printf("Invalid mode (-m)!\n");
        return 1;
    }
    printf("Using %s alignment mode.\n", mode);


    // Alphabet information, will be constructed on fly while reading sequences
    unsigned char letterIdx[128]; //!< letterIdx[c] is index of letter c in alphabet
    char idxToLetter[128]; //!< numToLetter[i] is letter that has index i in alphabet
    bool inAlphabet[128]; // inAlphabet[c] is true if c is in alphabet
    for (int i = 0; i < 128; i++) {
        inAlphabet[i] = false;
    }
    int alphabetLength = 0;

    int readResult;
    // Read queries
    char* queriesFilepath = argv[optind];
    vector< vector<unsigned char> >* querySequences = new vector< vector<unsigned char> >();
    printf("Reading queries...\n");
    readResult = readFastaSequences(queriesFilepath, querySequences, letterIdx, idxToLetter,
                                    inAlphabet, alphabetLength);
    if (readResult) {
        printf("Error: There is no file with name %s\n", queriesFilepath);
        delete querySequences;
        return 1;
    }
    int numQueries = querySequences->size();
    int queriesTotalLength = 0;
    for (int i = 0; i < numQueries; i++) {
        queriesTotalLength += (*querySequences)[i].size();
    }
    printf("Read %d queries, %d residues total.\n", numQueries, queriesTotalLength);

    // Read target
    char* targetFilepath = argv[optind+1];    
    vector< vector<unsigned char> >* targetSequences = new vector< vector<unsigned char> >();
    printf("Reading target fasta file...\n");
    readResult = readFastaSequences(targetFilepath, targetSequences, letterIdx, idxToLetter,
                                    inAlphabet, alphabetLength);
    if (readResult) {
        printf("Error: There is no file with name %s\n", targetFilepath);
        delete querySequences;
        delete targetSequences;
        return 1;
    }
    unsigned char* target = (*targetSequences)[0].data();
    int targetLength = (*targetSequences)[0].size();
    printf("Read target, %d residues.\n", targetLength);

    printf("Alphabet: ");
    for (int c = 0; c < 128; c++)
        if (inAlphabet[c])
            printf("%c ", c);
    printf("\n");

    TSequence* querySeqAn = new TSequence;
    for (int idx = 0; idx < (*querySequences)[0].size(); idx++) {
        seqan::appendValue(*querySeqAn, (*querySequences)[0][idx]);
    }
    TSequence* targetSeqAn = new TSequence;
    for (int idx = 0; idx < (*targetSequences)[0].size(); idx++) {
        seqan::appendValue(*targetSeqAn, (*targetSequences)[0][idx]);
    }
    TAlign* align = new TAlign;
    seqan::resize(seqan::rows(*align), 2);
    seqan::assignSource(seqan::row(*align, 0), *targetSeqAn);
    seqan::assignSource(seqan::row(*align, 1), *querySeqAn);

    // ----------------------------- MAIN CALCULATION ----------------------------- //
    printf("\nComparing queries to target...\n");
    int* scores = new int[numQueries];
    int** endLocations = new int*[numQueries];
    int** startLocations = new int*[numQueries];
    int* numLocations = new int[numQueries];
    priority_queue<int> bestScores; // Contains numBestSeqs best scores
    int k = kArg;
    unsigned char* alignment = NULL; int alignmentLength;
    clock_t start = clock();

    if (!findAlignment || silent) {
        printf("0/%d", numQueries);
        fflush(stdout);
    }
    for (int i = 0; i < numQueries; i++) {
        unsigned char* query = (*querySequences)[i].data();
        int queryLength = (*querySequences)[i].size();

        start = clock();
        // Calculate score
        if (useSimple) {
            // Just for testing
            /*
            calcEditDistanceSimple(query, queryLength, target, targetLength,
                                   alphabetLength, modeCode, scores + i,
                                   endLocations + i, numLocations + i);
            */
            int score;
            if (findAlignment) {
                if (modeCode == EDLIB_MODE_SHW) {
                    // Fails for larger sequences because it uses quadratic space. Nothing we can do about this.
                    score = seqan::globalAlignment(*align, seqan::Score<int, seqan::Simple>(0, -1, -1),
                                                   seqan::AlignConfig<false, false, false, true>(),
                                                   seqan::LinearGaps());
                }
                if (modeCode == EDLIB_MODE_HW) {
                    // Fails for larger sequences because it uses quadratic space. Nothing we can do about this.
                    score = seqan::globalAlignment(*align, seqan::Score<int, seqan::Simple>(0, -1, -1),
                                                   seqan::AlignConfig<true, false, false, true>(),
                                                   seqan::LinearGaps());
                }
                if (modeCode == EDLIB_MODE_NW) {
                    // Works well.
                    printf("\nStarted alignment\n");
                    score = seqan::globalAlignment(*align, seqan::MyersHirschberg());
                    printf("\nFinished alignment\n");
                }
            } else {
                if (modeCode == EDLIB_MODE_SHW) {
                    score = seqan::globalAlignmentScore(*targetSeqAn, *querySeqAn,
                                                        seqan::Score<int, seqan::Simple>(0, -1, -1),
                                                        seqan::AlignConfig<false, false, false, true>(),
                                                        seqan::LinearGaps());
                }
                if (modeCode == EDLIB_MODE_HW) {
                    score = seqan::globalAlignmentScore(*targetSeqAn, *querySeqAn,
                                                        seqan::Score<int, seqan::Simple>(0, -1, -1),
                                                        seqan::AlignConfig<true, false, false, true>(),
                                                        seqan::LinearGaps());
                    // Finder interface works with error limit - it finds all matches that have score smaller
                    // than given limit. To get this to work, we would need to start with small limit and increase
                    // it until it gives first results, and then find the smallest among them.
                    // That would still return a lot of scores that we would have to go through and find minimal.
                    // seqan::Pattern<TSequence, seqan::Myers<FindInfix> > pattern(*querySeqAn);
                    // seqan::Finder<TSequence> finder(*targetSeqAn);
                    // if (seqan::find(finder, pattern, -1000))
                    //     cout << "\nFound pattern with score: " << seqan::getScore(pattern) << endl;
                    // else
                    //     cout << "\nDidn't find pattern!" << endl;
                }
                if (modeCode == EDLIB_MODE_NW) {
                    // Works well.
                    score = seqan::globalAlignmentScore(*querySeqAn, *targetSeqAn, seqan::MyersBitVector());
                }
            }
            cout << "\n Seqan Score: " << score << endl;

        } else {
            edlibCalcEditDistance(query, queryLength, target, targetLength,
                                  alphabetLength, k, modeCode, findStartLocations, findAlignment,
                                  scores + i, endLocations + i, startLocations + i, numLocations + i,
                                  &alignment, &alignmentLength);
            cout << "\n Edlib Score: " << scores[i] << endl;
        }

        clock_t finish = clock();
        double cpuTime = ((double)(finish-start))/CLOCKS_PER_SEC;
        printf("\nCpu time of searching: %lf\n", cpuTime);
        exit(0);

    }
    
    return 0;
}




/** Reads sequences from fasta file.
 * Function is passed current alphabet information and will update it if needed.
 * @param [in] path Path to fasta file containing sequences.
 * @param [out] seqs Sequences will be stored here, each sequence as vector of indexes from alphabet.
 * @param [inout] letterIdx  Array of length 128. letterIdx[c] is index of letter c in alphabet.
 * @param [inout] inAlphabet  Array of length 128. inAlphabet[c] is true if c is in alphabet.
 * @param [inout] alphabetLength
 * @return 0 if all ok, positive number otherwise.
 */
int readFastaSequences(const char* path, vector< vector<unsigned char> >* seqs,
                       unsigned char* letterIdx, char* idxToLetter, bool* inAlphabet, int &alphabetLength) {
    seqs->clear();
    
    FILE* file = fopen(path, "r");
    if (file == 0)
        return 1;

    bool inHeader = false;
    bool inSequence = false;
    int buffSize = 4096;
    char buffer[buffSize];
    while (!feof(file)) {
        int read = fread(buffer, sizeof(char), buffSize, file);
        for (int i = 0; i < read; ++i) {
            char c = buffer[i];
            if (inHeader) { // I do nothing if in header
                if (c == '\n')
                    inHeader = false;
            } else {
                if (c == '>') {
                    inHeader = true;
                    inSequence = false;
                } else {
                    if (c == '\r' || c == '\n')
                        continue;
                    // If starting new sequence, initialize it.
                    if (inSequence == false) {
                        inSequence = true;
                        seqs->push_back(vector<unsigned char>());
                    }

                    if (!inAlphabet[c]) { // I construct alphabet on fly
                        inAlphabet[c] = true;
                        letterIdx[c] = alphabetLength;
                        idxToLetter[alphabetLength] = c;
                        alphabetLength++;
                    }
                    seqs->back().push_back(letterIdx[c]);
                }
            }
        }
    }

    fclose(file);
    return 0;
}


void printAlignment(const unsigned char* query, const int queryLength,
                    const unsigned char* target, const int targetLength,
                    const unsigned char* alignment, const int alignmentLength,
                    const int position, const int modeCode, const char* idxToLetter) {
    int tIdx = -1;
    int qIdx = -1;
    if (modeCode == EDLIB_MODE_HW) {
        tIdx = position;
        for (int i = 0; i < alignmentLength; i++) {
            if (alignment[i] != 1)
                tIdx--;
        }
    }
    for (int start = 0; start < alignmentLength; start += 50) {
        // target
        printf("T: ");
        int startTIdx;
        for (int j = start; j < start + 50 && j < alignmentLength; j++) {
            if (alignment[j] == 1)
                printf("_");
            else
                printf("%c", idxToLetter[target[++tIdx]]);
            if (j == start)
                startTIdx = tIdx;
        }
        printf(" (%d - %d)\n", max(startTIdx, 0), tIdx);
        // query
        printf("Q: ");
        int startQIdx = qIdx;
        for (int j = start; j < start + 50 && j < alignmentLength; j++) {
            if (alignment[j] == 2)
                printf("_");
            else
                printf("%c", idxToLetter[query[++qIdx]]);
            if (j == start)
                startQIdx = qIdx;
        }
        printf(" (%d - %d)\n\n", max(startQIdx, 0), qIdx);
    }
}
