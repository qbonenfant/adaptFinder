#include <seqan/index.h>
#include <seqan/seq_io.h>
#include <seqan/sequence.h>  
#include <seqan/arg_parse.h>

#include <iostream>
#include <cstdlib>  
#include <fstream>
#include <cmath>
#include <stdlib.h>
#include <time.h>
#include <stdexcept>
#include <unordered_map>


using namespace seqan;

// Program start timestamp
const auto boot_time = std::chrono::steady_clock::now();

// Alphabet used for 2 bit conversion
const std::string DNA = "ACGT";

// Setting the index
typedef FastFMIndexConfig<void, uint32_t, 2, 1> TFastConfig;
using index_t = Index<StringSet<DnaString>, BidirectionalIndex<FMIndex<void,TFastConfig> > >;

// counter type, using unordered map.
typedef std::unordered_map<uint,uint> counter;
// pair vector
typedef std::vector<std::pair<unsigned,unsigned> > pair_vector;
// type of sequence set
typedef StringSet<DnaString> sequence_set_type;
// vector of boolean used to keep track of kmer positions count.
typedef std::vector<bool>  bit_field;
// config file parameter map
typedef std::unordered_map<std::string, std::string> arg_map;



/**
    Convert a Seqan DnaString to unsigned int.
    SeqAn already store DnaString in 2 bit representation,
    but it is not easy to access it "as is" or to use it
    as key in a hash map.
    @param The sequence to convert
    @return the kmer in 2 bit format, as an unsigned int.
*/
inline unsigned dna2int(DnaString seq){
    
    unsigned value = 0;
    for(auto c : seq){
        value = value << 2 | (uint8_t)(c);
    }
    return(value);
}

/**
    Convert an unsigned int back to Seqan DnaString
    @param the integer to convert
    @param the size of the kmer
    @return the kmer in SeqAn DnaString format
*/
inline DnaString int2dna(unsigned value, uint8_t k){
    std::string seq = "";
    for(int i = 0; i< k; i++){
        seq = DNA[value & 3] + seq;
        value >>= 2;

    }
    return(DnaString(seq));
}


/**
    Shortcut to print text in stdout with a time stamp.
    @param whatever you want to print.
*/
template<typename TPrintType>
void print(TPrintType text, int tab = 0)
{   

    const auto milis = std::chrono::duration <double, std::milli>(std::chrono::steady_clock::now() - boot_time).count();
    std::cout << "[" << milis << " ms]\t" ;
    for(int i = 0; i < tab; i++){
        std::cout << "\t";
    }
    std::cout << text << std::endl;
}

/**
    Extremly simple config file parser.
    format  : args=value , one per line
    comments: #
    @param the path to config file
    @return a map containing the set parameters.
*/
arg_map parse_config(std::string inputFile){

    arg_map params;

    std::ifstream inFile;
    inFile.open(inputFile);
    if(inFile.is_open()){
        std::string line;
        while( std::getline(inFile, line) ){
            std::string arg = "";
            std::string val = "";
            bool sep = false;
            if(line[0] != '#'){
                for(auto c: line){
                    if(c == '='){
                        sep = true;
                    }
                    else if( c!= ' '){
                        if(sep)
                            val+=c;
                        else
                            arg+=c;
                    }
                }
                params[arg] = val;
            }
        }
    }
    else{
        std::cout << "could not open config file\n";
    }
    return(params);
}


/**
    Shortcut to print list of kmer counts (using either pairs or map )
    @param the counter to export (pair or map of kmer associated to their count)
    @param k, the size of the kmer counted (needed for conversion 2bit representation back to DNA)
*/
template<typename TPrintType>
void printCounters(TPrintType & pvec, uint8_t k){
    for(auto it = pvec.begin(); it!= pvec.end(); ++it)
    { 
        std::cout << int2dna(it->first,k) << " " << it->second << "\n";
    }   
}

/**
    Export a counter to a file
    @param the counter to export (pair or map of kmer associated to their count)
    @param k, the size of the kmer counted (needed for conversion 2bit representation back to DNA)
    @param the path to the outputfile
*/
template<typename TPrintType>
bool exportCounter(TPrintType & pvec, uint8_t k, std::string output){

    std::ofstream outputFile;
    outputFile.open (output);
    if(outputFile.is_open()){
        for(auto it = pvec.begin(); it!= pvec.end(); ++it)
        { 
            outputFile << int2dna(it->first,k) << "\t" << it->second << "\n";
        }   
        outputFile.close();
    }
    else{
        print("COULD NOT OPEN FILE " + output);
        return(false);
    }
    return(true);
}

/**
    Adjust low complexity threshold value to kmer size
    @param low complexity threshold for a size of kmer
    @param kmer size for this threshold
    @param new k-mer size
    @return the appropriate threshold value for the new kmer size.
*/
float adjust_threshold(float c_old, uint8_t k_old, uint8_t k_new ){
    float c_new = c_old * float( std::pow(k_new - 2 + 1,2) /  std::pow(k_old - 2 + 1,2));
    return(c_new);
}

/**
    Compute the sum of a vector (of int for example).
    @param the vector
    @return the sum
*/
template<typename TVector>
inline unsigned vectorSum(TVector vec)
{
    unsigned res = 0;
    for(auto it: vec)
    {
        res += it;
    }
    return(res);
}

/**
    Check the complexity of a kmer
    @param the kmer to test, in 2 bit representation, cast as an unsigned int
    @param k the size of said kmer
    @param threshold for the low complexity filter.
    @return True if the kmer contains low complexity region
*/
inline  bool haveLowComplexity(unsigned kmer, uint8_t k, float threshold){
    
    unsigned counts[16] = { 0 }; // 16 possibles dimers
    // reading using sliding window of 2
    for(int i = 0; i < k-1; i++){
        // storing dimers as 2 * 2 bits
        uint8_t c =  kmer & 15;
        // removing last element of the k-mer
        kmer >>=2;
        // updating value of dimer count
        counts[c]++;
    }

    float s = 0;
    size_t sum = 0;
    for(auto v:counts){
        sum +=  v * (v-1);  
    }
    s =  sum / float(2 * (k-2));
    return s>= threshold;
}

/**
    Return the first top kmers, ranked by count, descending.
    @param kmer count map (see count_kmer and error count)
    @param number of kmers to return
    @return vector of pair containing the most frequent kmers and their associated count.
*/
pair_vector get_most_frequent(counter & count_map, unsigned limit){

    pair_vector kmer_vec(std::make_move_iterator(count_map.begin()), std::make_move_iterator(count_map.end()));
    std::sort(kmer_vec.begin(), kmer_vec.end(), [](auto x, auto y){ return x.second > y.second;} );
    if(kmer_vec.size() > limit){
        kmer_vec.resize(limit);
    }
    return(kmer_vec);
}


/**
    Sample the sequences set and return requested samples cut to size
    @param Set of sequences to sample (SeqAn StringSet of DnaString)
    @param Number of sequences to sample
    @param Size of the sampled sequence.
    @return A set of sample sequences cut to size.
*/
sequence_set_type sampleSequences(sequence_set_type & sequence_set, unsigned nb_sample, unsigned cut_size, bool bot, uint8_t v){
    sequence_set_type sample;
    
    // Initialising the random seed
    srand(time(0));

    unsigned sequence_set_size = length(sequence_set);
    // Building vector with all possible seq indice
    std::vector<int> vec(sequence_set_size) ; 
    std::iota(std::begin(vec), std::end(vec), 0);

    // and applying random shuffling to said vector.
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(vec.begin(), vec.end(), g);

    // counters
    unsigned nb_seq = 0;
    unsigned i = 0;
    unsigned seq_id;

    // display
    if(v>0){
        if(bot){
            print("Sampling the ends of reads",1);
        }
        else{
            print("Sampling the start of reads",1);
        }
    }

    // Fetching the random sequences
    while(nb_seq < nb_sample and i < sequence_set_size ){
        
        // current sequence id
        seq_id = vec[i];

        // we also need to check that the sequence is
        // at least long enough to contains both adapters.
        if( length(sequence_set[ seq_id ]) >= cut_size * 2 ){
            if(bot){
                appendValue(sample, suffix(sequence_set[ seq_id ], length(sequence_set[ seq_id ]) - 1 - cut_size ));
            }
            else{
                appendValue(sample, prefix(sequence_set[ seq_id ],cut_size));
            }
            nb_seq +=1;
        }
        i++;
    }
    if(v>0)
        print("Sampled " + std::to_string(length(sample)) + " sequences",1);
    return(sample);
}


/**
    Perform a simple exact count of all the k-mers from a sample set of sequences
    @param Set of sequences (SeqAn StringSet of DnaString)
    @param k, size of the kmers
    @param threshold of the low complexity filter
    @return a map of the kmer count, with a kmer hash as key.

*/
counter count_kmers(sequence_set_type & sequences, uint8_t k, float threshold){

    counter count;
    unsigned base = std::pow(2,(2*k))-1;
    for(auto seq: sequences){
        // First kmer
        unsigned n = dna2int(DnaString(prefix(seq,k-1)));
        int i = k-1;
        while(i < length(seq)){ 
            
            n <<= 2; 
            n = (n & base) |  (uint8_t)(seq[i]);
            if(not haveLowComplexity(n,k,threshold)){
                count[n] +=1 ;
            }           
            i++;
        }
    }
    return(count);
}


/**
    Search and count a list of kmer in a set of sequences, at a Levenstein distance of at most 2.
    @param Set of sequences (SeqAn StringSet of DnaString)
    @param the previous count of exact kmer (the kmer list)
    @param number of thread to use
    @param k, size of the kmers
    @return a map of the kmer count, with a kmer hash as key.

*/
counter errorCount( sequence_set_type & sequences, pair_vector & exact_count, uint8_t nb_thread, uint8_t k, uint8_t v){
    
    const uint8_t MAXERR = 2; // Max number of errors, need to be fixed at compile time

    unsigned sample_size = length(sequences);
    if(v>0)
        print("Preparing index",1);
    index_t  index(sequences);
    
    if(v>0)
        print("Creating index",1);
    indexCreate(index);
    
    // Result storage
    counter results;

    // setting number of parallel thread
    omp_set_num_threads(nb_thread);
    if(v>0)
        print("Starting approximate counting",1);
    #pragma omp parallel shared(index, results)
    {
        // local variable to keep track of kmer occurences
        std::array<bit_field,3> tcount;

        // Delegate function for SeqAn find function (process occurences)
        auto delegateParallel = [& tcount](auto & iter, const DnaString & needle, int errors)
        {
            for (auto occ : getOccurrences(iter)){
                
                unsigned read_id = getValueI1(occ);
                tcount[errors][read_id] = true;
                }
        };

        #pragma omp for schedule(dynamic)
        for(int km_id=0; km_id<length(exact_count); km_id++)
        {
            
            /*  Supposing a kmer is very unlikely to be twice in the same read start
                we just store the read id in which kmer have been found using a bit field
                which size is the number of reads.
                Since we search at 2 error, we can either:
                    - Find the kmer exactly
                    - Find it at 1 error
                    - Find it at 2 errors
                So there is one bit field per number of error.
            */
            for(int i=0; i<3; i++){
                tcount[i] = bit_field(sample_size,false);
            }
            // 2 bit encoded kmer as unsigned int
            unsigned kmer = exact_count[km_id].first;
            // ressearch, filling tcount
            find<0, MAXERR >(delegateParallel, index, int2dna(kmer,k), EditDistance() );

            
            // computing total number of occurences
            unsigned total = 0;
            for(auto bit_count: tcount){
                total +=  vectorSum(bit_count);
            }
            // Updating global counter
            #pragma omp critical
            results[kmer] = total;
                
        }
    }
    return(results);
}




int main(int argc, char const ** argv)
{

    // Setup ArgumentParser.
    seqan::ArgumentParser parser("adaptFinder");

    addArgument(parser, seqan::ArgParseArgument(
        seqan::ArgParseArgument::STRING, "input filename"));

    addOption(parser, seqan::ArgParseOption(
        "lc", "low_complexity", "low complexity filter threshold (for k=16), default 1.5",
        seqan::ArgParseArgument::DOUBLE, "kmer filename"));
    
    addOption(parser, seqan::ArgParseOption(
        "sn", "sample_n", "sample n sequences from dataset, default 10k sequences",
        seqan::ArgParseArgument::INTEGER, "INT"));

    addOption(parser, seqan::ArgParseOption(
        "sl", "sample_length", "size of the sampled portion, default 100 bases",
        seqan::ArgParseArgument::INTEGER, "INT"));

    addOption(parser, seqan::ArgParseOption(
        "nt", "nb_thread", "Number of thread to work with, default is 4",
        seqan::ArgParseArgument::INTEGER, "INT"));

    addOption(parser, seqan::ArgParseOption(
        "k", "kmer_size", "Size of the kmers, default is 16",
        seqan::ArgParseArgument::INTEGER, "INT"));

    addOption(parser, seqan::ArgParseOption(
        "lim", "limit", "limit the number of kmer used after initial counting, default is 500",
        seqan::ArgParseArgument::INTEGER, "INT"));

     addOption(parser, seqan::ArgParseOption(
        "v", "verbosity", "Level of details printed out",
        seqan::ArgParseArgument::INTEGER, "INT"));

    addOption(parser, seqan::ArgParseOption(
        "e", "exact_file", "path to export the exact k-mer count, if needed. Default: no export",
        seqan::ArgParseArgument::STRING, "exact count output file"));

    addOption(parser, seqan::ArgParseOption(
        "conf", "config", "path to the config file",
        seqan::ArgParseArgument::STRING, "config file"));


    addOption(parser, seqan::ArgParseOption(
        "o", "out_file", "path to the output file, default is ./out.txt",
        seqan::ArgParseArgument::STRING, "output file"));

    // Parser command line.
    seqan::ArgumentParser::ParseResult res = seqan::parse(parser, argc, argv);

    // If parsing was not successful then exit with code 1. if there were errors.
    // Otherwise, exit with code 0 (e.g. help was printed).
    if (res != seqan::ArgumentParser::PARSE_OK)
        return res == seqan::ArgumentParser::PARSE_ERROR;

    // Extract option values and print them.
    std::string output = "out.txt";     // output file
    std::string exact_out;   // exact count output file
    std::string config_file; // configuration file
    unsigned nb_thread = 4;  // default number of thread
    unsigned k = 16;         // kmer size, 2<= k <= 32
    unsigned sl = 100 ;      // sequence sampling size
    unsigned sn = 10000;     // number of sequence sampled
    unsigned limit = 500;    // number of kmers to keep.
    double lc = 1.5;         // low complexity filter threshold, allow all known adapters to pass.
    unsigned v = 1;


    getOptionValue(config_file, parser, "conf");

    // reading config file, if any, and adjusting variables.
    if(not config_file.empty() ){
        arg_map params = parse_config(config_file);
        lc        = params.count("lc" )>0 ? std::stof(params["lc"] ) : lc;
        k         = params.count("k"  )>0 ? std::stoi(params["k"]  ) : k;
        v         = params.count("v"  )>0 ? std::stoi(params["v"]  ) : v;
        sn        = params.count("sn" )>0 ? std::stoi(params["sn"] ) : sn;
        sl        = params.count("sl" )>0 ? std::stoi(params["sl"] ) : sl;
        limit     = params.count("lim")>0 ? std::stoi(params["lim"]) : limit;
        nb_thread = params.count("nt" )>0 ? std::stoi(params["nt"] ) : limit;

        exact_out = params.count("e" )>0 ? params["e"] : exact_out;    
    }

    // If options have been manually set, override config.
    getOptionValue(limit, parser, "lim");
    getOptionValue(lc, parser, "lc");
    getOptionValue(k, parser, "k");
    getOptionValue(v, parser, "v");
    getOptionValue(sl, parser, "sl");
    getOptionValue(sn, parser, "sn");
    getOptionValue(nb_thread, parser, "nt");
    getOptionValue(output, parser, "o");
    getOptionValue(exact_out, parser, "e");
    std::string input_file;
    getArgumentValue(input_file, parser, 0);



    std::string warning = "/!\\ WARNING: ";

    // checking value for k
    if( k<2 or k>32 ){
        throw std::invalid_argument("kmer size must be between 2 and 32 (included)");
    }
    
    // print parameters
    if(v>0){
        std::cout << "Kmer size:             " << k         << std::endl;
        std::cout << "Sampled sequences:     " << sn        << std::endl;
        std::cout << "Sampling length        " << sl        << std::endl;
        std::cout << "Number of kept kmer:   " << limit     << std::endl;
        std::cout << "LC filter threshold:   " << lc        << std::endl;
        std::cout << "Nb thread:             " << nb_thread << std::endl;
        std::cout << "Verbosity level:       " << v         << std::endl;
    }

    // number of tab to display
    int tab_level = 0;
    // adjusting low complexity to kmer size
    lc = adjust_threshold( lc, 16, k );
    if(v>0)
        std::cout << "Adjusted LC threshold: " << lc << std::endl;



    // Parsing input fasta file.
    // It may be replaced by a custom version
    // because SeqAn seems to use a lot of RAM.
    StringSet<CharString> ids;
    StringSet<DnaString> seqs;
    if(v>0)
        print("Parsing FASTA file",tab_level);
    SeqFileIn seqFileIn(toCString(input_file));
    readRecords(ids, seqs, seqFileIn);
    
    // Checking if we can sample the requested number of sequences, else return the whole set
    unsigned sequence_set_size = length(seqs);
    if( v>0 and sn > sequence_set_size){ 
        std::cout << warning << "Sequence set too small for the requested sample size\n";
        std::cout << warning << "The whole set will be used.\n" ;
        sn = sequence_set_size;
    }



    bool success = true;
    // performing ressearch on both ends
    std::array<std::string, 2 > ends = {"start","end"};
    bool bottom = false; // checking if we search top adapter(start) or bottom adapter (end)
    for(std::string which_end: ends){

        tab_level += 1;
        if(v>0){
            print("Working on " + which_end + " adapter",tab_level - 1);
            // sample and cut sequences to required length
            print("Sampling",tab_level);
        }
        sequence_set_type sample = sampleSequences(seqs, sn, sl, bottom, v);


        // counting k-mers on the sampled sequences
        if(v>0)
            print("Exact k-mer count",tab_level);
        counter count = count_kmers(sample, k, lc);
        

        // keeping most frequents kmers
        if(v>0)
            print("Number of kmer found: " + std::to_string(count.size()), tab_level);
        pair_vector first_n_vector = get_most_frequent(count, limit);
        if(v>0)
            print("Number of kmer kept:  " + std::to_string(first_n_vector.size()), tab_level ) ;
        
        // Just print a warning if we think adapter may have been trimmed.
        if( v>0 and  first_n_vector[0].second < 0.1 * sn){
            std::cout << warning << "The most frequent kmer has been found in less than 10% of the reads (" << first_n_vector[0].second << "/" << sn << ")" <<std::endl;
            std::cout << warning << "It could mean this file is already trimmed or the sample do not contains detectable adapters." << std::endl;
        }

        // Exporting exact kmer count, if required
        if(not exact_out.empty() ){
            if(v>0)
                print("Exporting exact kmer count",tab_level);
            success = exportCounter(first_n_vector, k, exact_out + "." + which_end );
            if(!success){
                return(1);
            }
        }

        // Counting with at most 2 errors
        if(v>0)
            print("Approximate k-mer count",tab_level);
        counter error_counter = errorCount(sample, first_n_vector, nb_thread, k, v);
        pair_vector sorted_error_count = get_most_frequent(error_counter,limit);

        if(v>0)
            print("Exporting approximate count",tab_level);
        success = exportCounter(sorted_error_count,k, output+ "." + which_end );
        if(!success){
                return(1);
            }
        if(v>0)
            print("Done",tab_level);
        
        clear(sample);
        bottom = true;
        tab_level -= 1;
    }
    

    return 0;
}