#ifdef TENSORFLOW
#include <vector>

#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/public/session.h"

#include "common.h"
#include "egbbdll.h"

using namespace tensorflow;

static Session* session;
static const string main_input_layer = "main_input";
static const string aux_input_layer = "aux_input";
static const string output_layer = "value_0";
static const int CHANNELS = 12;
static const int PARAMS = 5;

struct InputData {
    Tensor* main_input;
    Tensor* aux_input;
    int n_batch;

    InputData() {
        main_input = 0;
        aux_input = 0;
        n_batch = 0;
    }
    void alloc() {
        static const int MAX_BATCH = 256;
        static const TensorShape main_input_shape({MAX_BATCH, 8, 8, CHANNELS});
        static const TensorShape aux_input_shape({MAX_BATCH, PARAMS});
        main_input = new Tensor(DT_FLOAT, main_input_shape);
        aux_input = new Tensor(DT_FLOAT, aux_input_shape);
    }
};

static std::unordered_map<int,InputData> input_map;

/*
   Load NN
*/
static Status LoadGraph(const string& graph_file_name,
        Session** session) {

    GraphDef graph_def;
    Status load_graph_status =
        ReadBinaryProto(Env::Default(), graph_file_name, &graph_def);
    if (!load_graph_status.ok()) {
        return errors::NotFound("Failed to load compute graph at '",
                graph_file_name, "'");
    }

    SessionOptions options;
    options.config.set_intra_op_parallelism_threads(1);
    options.config.set_inter_op_parallelism_threads(1);

    Status status = NewSession(options, session);
    Status session_create_status = (*session)->Create(graph_def);
    if (!session_create_status.ok()) {
        return session_create_status;
    }

    return Status::OK();
}

/*
   Initialize tensorflow
*/
DLLExport void CDECL load_neural_network(char* path) {
    printf("Loading neural network....\n");
    fflush(stdout);

#ifdef _WIN32
    SetEnvironmentVariable((LPCWSTR)"TF_CPP_MIN_LOG_LEVEL",(LPCWSTR)"3");
#else
    setenv("TF_CPP_MIN_LOG_LEVEL","3",1);
#endif

    TF_CHECK_OK( LoadGraph(path, &session) );



    /*warm up*/
    int piece = _EMPTY, square = _EMPTY;
    probe_neural_network(0, &piece, &square);

    printf("Neural network loaded !      \n");
    fflush(stdout);
}

/*
   Fill input planes
*/
static void fill_input_planes(int player, int* piece,int* square, float* data, float* adata) {
    int pc, col, sq, to;
    int board[128];

    /*zero board*/
    memset(board, 0, sizeof(int) * 128);
    memset(data,  0, sizeof(float) * 8 * 8 * CHANNELS);
    memset(adata, 0, sizeof(float) * PARAMS);

    /*fill board*/
    int ksq = SQ6488(square[player]);
    bool fliph = (file(ksq) <= FILED);

    for(int i = 0; (pc = piece[i]) != _EMPTY; i++) {
        sq = SQ6488(square[i]);
        if(player == _BLACK) {
            sq = MIRRORR(sq);
            pc = invert_color(pc);
        }
        if(fliph) 
            sq = MIRRORF(sq);

        board[sq] = pc;
    }

    /* 
       Add the attack map planes 
    */
#define D(sq,C)     data[rank(sq) * 8 * CHANNELS + file(sq) * CHANNELS + C]

#define NK_MOVES(dir, off) {                    \
        to = sq + dir;                          \
        if(!(to & 0x88)) D(to, off) = 1.0f;     \
}

#define BRQ_MOVES(dir, off) {                   \
        to = sq + dir;                          \
        while(!(to & 0x88)) {                   \
            D(to, off) = 1.0f;                  \
            if(board[to] != 0) break;           \
                to += dir;                      \
        }                                       \
}

    for(int i = 0; (pc = piece[i]) != _EMPTY; i++) {

        sq = SQ6488(square[i]);
        if(player == _BLACK) {
            sq = MIRRORR(sq);
            pc = invert_color(pc);
        }
        if(fliph) 
            sq = MIRRORF(sq);

        switch(pc) {
            case wking:
                NK_MOVES(RU,0);
                NK_MOVES(LD,0);
                NK_MOVES(LU,0);
                NK_MOVES(RD,0);
                NK_MOVES(UU,0);
                NK_MOVES(DD,0);
                NK_MOVES(RR,0);
                NK_MOVES(LL,0);
                break;
            case wqueen:
                BRQ_MOVES(RU,1);
                BRQ_MOVES(LD,1);
                BRQ_MOVES(LU,1);
                BRQ_MOVES(RD,1);
                BRQ_MOVES(UU,1);
                BRQ_MOVES(DD,1);
                BRQ_MOVES(RR,1);
                BRQ_MOVES(LL,1);
                break;
            case wrook:
                BRQ_MOVES(UU,2);
                BRQ_MOVES(DD,2);
                BRQ_MOVES(RR,2);
                BRQ_MOVES(LL,2);
                break;
            case wbishop:
                BRQ_MOVES(RU,3);
                BRQ_MOVES(LD,3);
                BRQ_MOVES(LU,3);
                BRQ_MOVES(RD,3);
                break;
            case wknight:
                NK_MOVES(RRU,4);
                NK_MOVES(LLD,4);
                NK_MOVES(RUU,4);
                NK_MOVES(LDD,4);
                NK_MOVES(LLU,4);
                NK_MOVES(RRD,4);
                NK_MOVES(RDD,4);
                NK_MOVES(LUU,4);
                break;
            case wpawn:
                NK_MOVES(RU,5);
                NK_MOVES(LU,5);
                break;
            case bking:
                NK_MOVES(RU,6);
                NK_MOVES(LD,6);
                NK_MOVES(LU,6);
                NK_MOVES(RD,6);
                NK_MOVES(UU,6);
                NK_MOVES(DD,6);
                NK_MOVES(RR,6);
                NK_MOVES(LL,6);
                break;
            case bqueen:
                BRQ_MOVES(RU,7);
                BRQ_MOVES(LD,7);
                BRQ_MOVES(LU,7);
                BRQ_MOVES(RD,7);
                BRQ_MOVES(UU,7);
                BRQ_MOVES(DD,7);
                BRQ_MOVES(RR,7);
                BRQ_MOVES(LL,7);
                break;
            case brook:
                BRQ_MOVES(UU,8);
                BRQ_MOVES(DD,8);
                BRQ_MOVES(RR,8);
                BRQ_MOVES(LL,8);
                break;
            case bbishop:
                BRQ_MOVES(RU,9);
                BRQ_MOVES(LD,9);
                BRQ_MOVES(LU,9);
                BRQ_MOVES(RD,9);
                break;
            case bknight:
                NK_MOVES(RRU,10);
                NK_MOVES(LLD,10);
                NK_MOVES(RUU,10);
                NK_MOVES(LDD,10);
                NK_MOVES(LLU,10);
                NK_MOVES(RRD,10);
                NK_MOVES(RDD,10);
                NK_MOVES(LUU,10);
                break;
            case bpawn:
                NK_MOVES(RD,11);
                NK_MOVES(LD,11);
                break;
        }

        col = COLOR(pc);
        pc = PIECE(pc);

        if(pc != king) {
            if(col == white)
                adata[pc - queen]++;
            else
                adata[pc - queen]--;
        }
    }

#undef NK_MOVES
#undef BRQ_MOVES
#undef D

}

/*
   Convert winning percentage to centi-pawns
*/
static const double Kfactor = -log(10.0) / 400.0;

static inline int logit(double p) {
    if(p < 1e-15) p = 1e-15;
    else if(p > 1 - 1e-15) p = 1 - 1e-15;
    return int(log((1 - p) / p) / Kfactor);
}

/*
   Add position to batch
*/

DLLExport void CDECL add_to_batch(int player, int* piece,int* square) {

    //get identifier
    int tid = GETTID();
    InputData& inp = input_map[tid];
    if(!inp.main_input) inp.alloc();

    //input
    float* minput = (float*)(inp.main_input->tensor_data().data());
    float* ainput = (float*)(inp.aux_input->tensor_data().data());

    minput += inp.n_batch * (8 * 8 * CHANNELS);
    ainput += inp.n_batch * (PARAMS);

    //fill planes
    fill_input_planes(player, piece, square, minput, ainput);
    inp.n_batch++;
}

/*
   Do batch inference
*/

DLLExport void CDECL probe_neural_network_batch(int* scores) {

    //get identifier
    int tid = GETTID();
    InputData& inp = input_map[tid];

    //input
    Tensor minput = Tensor(DT_FLOAT, {inp.n_batch, 8, 8, CHANNELS});
    Tensor ainput = Tensor(DT_FLOAT, {inp.n_batch, PARAMS});

    float* mp = (float*)(minput.tensor_data().data());
    float* ap = (float*)(ainput.tensor_data().data());
    float* mpb = (float*)(inp.main_input->tensor_data().data());
    float* apb = (float*)(inp.aux_input->tensor_data().data());
    memcpy(mp, mpb, inp.n_batch*8*8*CHANNELS*sizeof(float) );
    memcpy(ap, apb, inp.n_batch*PARAMS*sizeof(float) );

    //outputs
    std::vector<Tensor> outputs;

    //run session
    std::vector<std::pair<string, Tensor> > inputs = {
        {main_input_layer, minput},
        {aux_input_layer,  ainput}
    };

    TF_CHECK_OK( session->Run(inputs, {output_layer}, {}, &outputs) );

    //extract and return score in centi-pawns
    for(int i = 0;i < inp.n_batch; i++) {
        float p = outputs[0].flat<float>()(i);
        scores[i] = logit(p);
    }

    inp.n_batch = 0;
}

/*
   Evaluate position using NN
*/

DLLExport int CDECL probe_neural_network(int player, int* piece,int* square) {
    int score;
    add_to_batch(player,piece,square);
    probe_neural_network_batch(&score);
    return score;
}

#endif