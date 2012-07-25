#include "common.h"
#include "egbbdll.h"

/*
globals
*/
static const int WIN_SCORE  =  5000;

enum {
	DECOMP_IN_RAM,DECOMP_IN_DISK,COMP_IN_RAM,COMP_IN_DISK
};
static const int VALUE[4] = {
	DRAW, WIN, LOSS, PREDICTED
};

#define is_in_disk(x)    ((x) & 1)
#define is_comp(x)       ((x) & 2)

static SEARCHER searchers[8];
static LOCK searcher_lock;

/*
EGBB
*/
char EGBB::path[256];
map<int,EGBB*> egbbs;

int EGBB::GetIndex(ENUMERATOR* penum) {
	penum->sort(0);
	register int index = penum->player,prd = 2,pic;
	for(int i = 0;i < penum->n_piece;i++) {
		pic = penum->piece[i];
		if(PIECE(pic) == king) continue;
		index += pic * prd;
		prd *= 14;
	}
	return index;
}
/*
Open EGBB
*/
void EGBB::open(int egbb_state) {
    UBMP32 i;
	
    //load file according to egbb_state	
	state = egbb_state;
	is_loaded = false;

	strcpy(name,path);
	strcat(name,enumerator.name);
	if(is_comp(state)) {
		strcat(name,".cmp");
	} else {
		strcat(name,".bin");
	}

	pf = fopen(name,"rb");
	if(!pf) {
		return;
	}

	//Decompresed in ram/disk
	if(!is_comp(state)) {  

		//get size of file and allocate buffers
		UBMP32 TB_SIZE;
		fseek(pf,0,SEEK_END);
		TB_SIZE = ftell(pf);
		fseek(pf,0,SEEK_SET);
		
		//load in ram
		if(state == DECOMP_IN_RAM) {
			table = new UBMP8[TB_SIZE];
			for(i = 0;i < TB_SIZE;i++)
				table[i] = fgetc(pf);
		}
	//compresed in ram/disk
	} else {   
		if(!COMP_INFO::open(pf))
			return;

		//compressed files in RAM
		if(state == COMP_IN_RAM) {
			table = new UBMP8[cmpsize];
			
			for(i = 0;i < cmpsize;i++) {
				table[i] = fgetc(pf);
			}
		}
	}

	is_loaded = true;
}
/*
get score of indexed position
*/
int EGBB::get_score(MYINT index,PSEARCHER psearcher) {
	int score;
	MYINT q;
	UBMP32 r;
	UBMP8 value;

	q = (index / 4);
	r = UBMP32(index % 4);

	if(state == DECOMP_IN_RAM) {
        value = table[q];
	} else if(state == DECOMP_IN_DISK) {
		UBMP32 block_start = UBMP32((q / BLOCK_SIZE) * BLOCK_SIZE);
		UBMP32 probe_index = UBMP32(q - block_start);
		
		psearcher->info.start_index = block_start;
		
        if(LRUcache.get(psearcher->info.start_index,probe_index,value) == CACHE_HIT) {
		} else {
			l_lock(lock);
			fseek(pf,block_start,SEEK_SET);
			fread(&psearcher->info.block,BLOCK_SIZE,1,pf);
			l_unlock(lock);
			value = psearcher->info.block[probe_index];
            LRUcache.add(&psearcher->info);
		}
	} else if(state == COMP_IN_RAM) {
		UBMP32 block_size;
		UBMP32 n_blk = UBMP32(q / BLOCK_SIZE);
		UBMP32 probe_index = UBMP32(q - (n_blk * BLOCK_SIZE));
		
		psearcher->info.start_index = index_table[n_blk];
		
		if(LRUcache.get(psearcher->info.start_index,probe_index,value) == CACHE_HIT) {
		} else {
			block_size = index_table[n_blk + 1] - index_table[n_blk];
			block_size = decode(&table[psearcher->info.start_index],psearcher->info.block,block_size);
			value = psearcher->info.block[probe_index];
			LRUcache.add(&psearcher->info);
		}
	}  else if(state == COMP_IN_DISK) {
		UBMP32 block_size;
		UBMP32 n_blk = UBMP32(q / BLOCK_SIZE);
		UBMP32 probe_index = UBMP32(q - (n_blk * BLOCK_SIZE));

		psearcher->info.start_index = index_table[n_blk];
		
		if(LRUcache.get(psearcher->info.start_index,probe_index,value) == CACHE_HIT) {
		} else {
			
			block_size = index_table[n_blk + 1] - index_table[n_blk];

			l_lock(lock);
			fseek(pf,read_start + psearcher->info.start_index,SEEK_SET);
			fread(psearcher->temp_block,block_size,1,pf);
			l_unlock(lock);

        	block_size = decode(psearcher->temp_block,psearcher->info.block,block_size);

			value = psearcher->info.block[probe_index];
			LRUcache.add(&psearcher->info);
		}
	}

	score = VALUE[(value >> (r << 1)) & 3];

    return score;
}
/*ADD files*/
#define ADD() {                                   \
	pegbb[side] = new EGBB;                       \
	penum = &pegbb[side]->enumerator;             \
	penum->add(side,piece);                       \
	tab_index[side] = EGBB::GetIndex(penum);      \
	egbbs[tab_index[side]] = pegbb[side];         \
	penum->sort(1);                               \
	penum->init();                                \
	pegbb[side]->open(state);                     \
};

/*One side bitbases*/
#define PRESENCE() {                              \
	if(pegbb[0]->is_loaded) {                     \
		if(!pegbb[1]->is_loaded)                  \
			pegbb[1]->use_search = true;          \
	} else if(pegbb[1]->is_loaded) {              \
		pegbb[0]->use_search = true;              \
	} else {                                      \
		delete pegbb[0];						  \
		delete pegbb[1];                          \
		egbbs[tab_index[0]] = 0;                  \
		egbbs[tab_index[1]] = 0;                  \
	}                                             \
};
/*
Open EGBB files and allocate cache
*/
void load_egbb_xxx(char* path,int cache_size,int load_options) {
	EGBB* pegbb[2];
	ENUMERATOR* penum;
	int side,piece[MAX_PIECES],state,state1,state2,tab_index[2];

	if(load_options == LOAD_NONE) {
        state1 = COMP_IN_DISK;
		state2 = COMP_IN_DISK;
	} else if(load_options == LOAD_4MEN) {
		state1 = COMP_IN_RAM;
		state2 = COMP_IN_DISK;
	} else if(load_options == SMART_LOAD) {
		state1 = COMP_IN_RAM;
		state2 = COMP_IN_DISK;
	} else if(load_options == LOAD_5MEN) {
		state1 = COMP_IN_RAM;
		state2 = COMP_IN_RAM;
	}
	strcpy(EGBB::path,path);

	printf("EgbbProbe 4.0 by Daniel Shawul\n");
	fflush(stdout);

	init_sqatt();
	init_indices();
	LRU_CACHE::alloc( cache_size );
    l_create(searcher_lock);

	printf("Loading egbbs....");
	fflush(stdout);

	/*pieces*/
	piece[0] = wking;
	piece[1] = bking;
	int& piece1 = piece[2];
	int& piece2 = piece[3];
	int& piece3 = piece[4];
	int& piece4 = piece[5];

	state = state1;
	/*3 piece*/
	piece[3] = 0;
	for(piece1 = wqueen; piece1 <= wpawn; piece1++) {
		for(side = white; side <= black;side++) ADD();
		PRESENCE();
	}
	/*4 piece*/
	piece[4] = 0;
	for(piece1 = wqueen; piece1 <= wpawn; piece1++) {
		for(piece2 = piece1; piece2 <= wpawn; piece2++) {
			for(side = white; side <= black;side++) ADD();
			PRESENCE();
		}
	}
	for(piece1 = wqueen; piece1 <= wpawn; piece1++) {
		for(piece2 = COMBINE(black,PIECE(piece1)); piece2 <= bpawn; piece2++) {
			for(side = white; side <= black;side++) ADD();
			PRESENCE();
		}
	}
	state = state2;
	/*5 piece*/
	piece[5] = 0;
	for(piece1 = wqueen; piece1 <= wpawn; piece1++) {
		for(piece2 = piece1; piece2 <= wpawn; piece2++) {
			for(piece3 = piece2; piece3 <= wpawn; piece3++) {
				for(side = white; side <= black;side++) ADD();
				PRESENCE();
			}
		}
	}
	for(piece1 = wqueen; piece1 <= wpawn; piece1++) {
		for(piece2 = piece1; piece2 <= wpawn; piece2++) {
			for(piece3 = bqueen; piece3 <= bpawn; piece3++) {
				for(side = white; side <= black;side++) ADD();
				PRESENCE();
			}
		}
	}
	/*6 piece*/
	piece[6] = 0;
	for(piece1 = wqueen; piece1 <= wpawn; piece1++) {
		for(piece2 = piece1; piece2 <= wpawn; piece2++) {
			for(piece3 = bqueen; piece3 <= bpawn; piece3++) {
				for(piece4 = piece3; piece4 <= bpawn; piece4++) {
					for(side = white; side <= black;side++) ADD();
					PRESENCE();
				}
			}
		}
	}
	for(piece1 = wqueen; piece1 <= wpawn; piece1++) {
		for(piece2 = piece1; piece2 <= wpawn; piece2++) {
			for(piece3 = piece2; piece3 <= wpawn; piece3++) {
				for(piece4 = bqueen; piece4 <= bpawn; piece4++) {
					for(side = white; side <= black;side++) ADD();
					PRESENCE();
				}
			}
		}
	}
	for(piece1 = wqueen; piece1 <= wpawn; piece1++) {
		for(piece2 = piece1; piece2 <= wpawn; piece2++) {
			for(piece3 = piece2; piece3 <= wpawn; piece3++) {
				for(piece4 = piece3; piece4 <= wpawn; piece4++) {
					for(side = white; side <= black;side++) ADD();
					PRESENCE();
				}
			}
		}
	}
	/*7 men*/
	printf("\rEgbbs loaded !      \n");
	fflush(stdout);
}
/*
 * Exported functions
 */
DLLExport void CDECL open_egbb(int* piece) {
	EGBB* pegbb[2];
	ENUMERATOR* penum;
	int side,state = COMP_IN_DISK,tab_index[2];

	for(side = white; side <= black;side++) ADD();
	PRESENCE();
}
DLLExport void CDECL load_egbb_into_ram(int side,int* piece) {
	ENUMERATOR myenum;
	myenum.add(side,piece);
	EGBB* pegbb = egbbs[EGBB::GetIndex(&myenum)];
	if(pegbb->state != COMP_IN_RAM) {
		pegbb->table = new UBMP8[pegbb->cmpsize];
		for(UBMP32 i = 0;i < pegbb->cmpsize;i++) {
			pegbb->table[i] = fgetc(pegbb->pf);
		}
		pegbb->state = COMP_IN_RAM;
	}
}

DLLExport void CDECL unload_egbb_from_ram(int side,int* piece) {
	ENUMERATOR myenum;
	myenum.add(side,piece);
	EGBB* pegbb = egbbs[EGBB::GetIndex(&myenum)];
    if(pegbb->state == COMP_IN_RAM) {
		pegbb->state = COMP_IN_DISK;
		delete[] (pegbb->table);
	}
}
#undef ADD
#undef PRESENCE
/*
Getscore of position. Use search whenever possible.
*/
int SEARCHER::get_score(
               int alpha,int beta,
			   int side, int* piece,int* square
			   ) {
    /*KK*/
	if(piece[2] == 0)
		return DRAW;

	/*get score*/
	int i,j,score,legal_moves;
	int move,from,to;

	MYINT pos_index;
	UBMP32 tab_index;
	EGBB* pegbb;

	get_index(pos_index,tab_index,
		      side,piece,square);

	pegbb = egbbs[tab_index];
	if(!pegbb)
		return DONT_KNOW;

	/*is egbb loaded*/
	if(pegbb->is_loaded) {
		score = pegbb->get_score(pos_index,this);
		return score;

    /*recursive search*/
	} else if(pegbb->use_search) {
		int my_pic[MAX_PIECES],my_sq[MAX_PIECES];

        //64 to 88
		for(i = 0;i < MAX_PIECES && piece[i];i++) {
			my_pic[i] = piece[i];
			my_sq[i] = SQ6488(square[i]);
		}
		my_pic[i] = 0;

		//set up position if we are at the root
		if(ply == 0)
			set_pos(side,my_pic,my_sq);
		

		//generate moves and search
		pstack->count = 0;
		gen_all();
		legal_moves = 0;

		for(j = 0;j < pstack->count; j++) {

			//88 to 64
			for(i = 0;i < MAX_PIECES && piece[i];i++) {
				my_sq[i] = SQ8864(square[i]);
			}

			//move
			move = pstack->move_st[j];
			do_move(move);
			if(attacks(player,plist[COMBINE(opponent,king)]->sq)) {
				undo_move(move);
				continue;
			}

			legal_moves++;

			from = SQ8864(m_from(move));
			to = SQ8864(m_to(move));

			//remove captured piece
			if(m_capture(move)) {
				for(i = 0;i < MAX_PIECES;i++) {
					if(my_sq[i] == to) {
						for(;i < MAX_PIECES;i++) {
							my_pic[i] = my_pic[i + 1];
							my_sq[i] = my_sq[i + 1];
						}
					}
				}
			}
			//move piece
			for(i = 0;i < MAX_PIECES;i++) {
				if(my_sq[i] == from) {
					my_sq[i] = to;
					break;
				}
			}
			
			//recursive call
			score = -get_score(-beta,-alpha,player,my_pic,my_sq);

			undo_move(move);

			//update alpha
			if(score > alpha) { 
				alpha = score;
				if(score >= beta) {
					return beta;
				}
			}
		}

		//mate/stalemate?
		if(legal_moves == 0) {
			if(attacks(opponent,plist[COMBINE(player,king)]->sq))
				return LOSS;
			else
				return DRAW;
		}

		return alpha;
	} else {
        return DONT_KNOW;
	}
}
/*
King square tables
*/
static const int piece_cv[14] = {0,0,9,5,3,3,1,0,-9,-5,-3,-3,-1,0};
static const int  king_pcsq[64] = {
	-3,-2,-1,  0,  0,-1,-2,-3,  
	-2,-1,  0, 1, 1,  0,-1,-2,  
	-1,  0, 1, 2, 2, 1,  0,-1,  
	  0, 1, 2, 3, 3, 2, 1,  0,  
	  0, 1, 2, 3, 3, 2, 1,  0,  
	-1,  0, 1, 2, 2, 1,  0,-1,  
	-2,-1,  0, 1, 1,  0,-1,-2,  
	-3,-2,-1,  0,  0,-1,-2,-3
};

static const int  kbnk_pcsq[64] = {
	 7, 6, 5, 4, 3, 2, 1, 0,
     6, 7, 6, 5, 4, 3, 2, 1,  
     5, 6, 7, 6, 5, 4, 3, 2,  
     4, 5, 6, 7, 6, 5, 4, 3,  
     3, 4, 5, 6, 7, 6, 5, 4,  
     2, 3, 4, 5, 6, 7, 6, 5,  
	 1, 2, 3, 4, 5, 6, 7, 6,  
	 0, 1, 2, 3, 4, 5, 6, 7  
};
/*
 * Evaluate for progress
 */
static int eval(int player,int* piece,int* square,int wdl_score) {
	register int i,score,all_c,p_dist,material,ktable,w_king,b_king;

	//king locations
	for(i = 0;i < MAX_PIECES && piece[i];i++) {
		if(piece[i] == wking) w_king = square[i];
		if(piece[i] == bking) b_king = square[i];
	}
	all_c = i;
	
	//material
	material = 0;
	for(i = 0;i < all_c;i++) material += piece_cv[piece[i]];
	if(player == black) material = -material;
	
	//passed pawn
	int prom_score,wp_prom = 0,bp_prom = 0,wp_dist = 0,bp_dist = 0;
	
	for(i = 0;i < all_c;i++) {
		if(PIECE(piece[i]) == pawn) {
			if(COLOR(piece[i]) == white) wp_prom += rank64(square[i]);
			else bp_prom += (RANK7 - rank64(square[i]));
			wp_dist += distance(SQ6488(square[i]),SQ6488(w_king));
			bp_dist += distance(SQ6488(square[i]),SQ6488(b_king));
		}
	}
	if(player == white) p_dist = 2 * wp_dist - bp_dist;
	else p_dist = 2 * bp_dist - wp_dist;
	prom_score = wp_prom - bp_prom;
	if(player == black) prom_score = -prom_score;
	
	
	//king square table for kbnk and others
	int piece1 = piece[2],piece2 = piece[3];
	int square1 = square[2],square2 = square[3];
	if(all_c == 4 
		&& (((piece1 == wbishop && piece2 == wknight) ||
			     (piece1 == wknight && piece2 == wbishop))  
				 ||
				 ((piece1 == bbishop && piece2 == bknight) ||
				 (piece1 == bknight && piece2 == bbishop)))
				 ) {
		
		//get the king's & bishop's square
		int b_sq,loser_ksq,winner_ksq;
		if(COLOR(piece1) == white) {
			winner_ksq = w_king;
			loser_ksq = b_king;
			if(piece1 == wbishop) {
				b_sq = square1;
			} else {
				b_sq = square2;
			}
		} else {
			winner_ksq = b_king;
			loser_ksq = w_king;
			if(piece1 == bbishop) {
				b_sq = square1;
			} else {
				b_sq = square2;
			}
		}
		
		if(is_light64(b_sq));
		else {
			winner_ksq = MIRRORF64(winner_ksq); 
			loser_ksq = MIRRORF64(loser_ksq);
		}
		
		//score
		if(wdl_score == WIN) {
			ktable = king_pcsq[winner_ksq] - kbnk_pcsq[loser_ksq];
		} else {
			ktable = -king_pcsq[winner_ksq] + kbnk_pcsq[loser_ksq];
		}
		
	} else {
		if(player == white) {
			if(wdl_score == WIN) {
				ktable = - 2 * king_pcsq[b_king] + king_pcsq[w_king];
			} else {
				ktable = + 2 * king_pcsq[w_king] - king_pcsq[b_king];
			}
		} else {
			if(wdl_score == WIN) {
				ktable = - 2 * king_pcsq[w_king] + king_pcsq[b_king];
			} else {
				ktable = + 2 * king_pcsq[b_king] - king_pcsq[w_king];
			}
		}
	}
	
	//combine score
	if(player == white) {
		if(wdl_score == WIN) {
			score = 
				+ WIN_SCORE
				+ (5 - all_c) * 200 
				- distance(SQ6488(w_king),SQ6488(b_king)) * 2
				+ ktable * 5
				+ material * 50
				+ prom_score * 20
				- p_dist * 7;
		} else {
			score = 
				- WIN_SCORE
				- (5 - all_c) * 200
				+ distance(SQ6488(w_king),SQ6488(b_king)) * 2
				+ ktable * 5
				+ material * 50
				+ prom_score * 20
				- p_dist * 7;
		}
	} else {
		if(wdl_score == WIN) {
			score = 
				+ WIN_SCORE
				+ (5 - all_c) * 200
				- distance(SQ6488(w_king),SQ6488(b_king)) * 2
				+ ktable * 5
				+ material * 50
				+ prom_score * 20
				- p_dist * 7;
		} else {
			score = 
				- WIN_SCORE
				- (5 - all_c) * 200
				+ distance(SQ6488(w_king),SQ6488(b_king)) * 2
				+ ktable * 5
				+ material * 50
				+ prom_score * 20
				- p_dist * 7;
		}
	}
	return score;
}
/*
 * Proge egbb
 */
int probe_egbb_xxx(int player,int* piece,int* square) {

	register int score;

	PSEARCHER psearcher;
	l_lock(searcher_lock);
	for(int i = 0;i < 8;i++) {
		if(!searchers[i].used) {
			psearcher = &searchers[i];
			psearcher->used = 1;
			break;
		}
	}
	l_unlock(searcher_lock);
	
	score = psearcher->get_score(
		      LOSS,WIN,
		      player,piece,square);

    psearcher->used = 0;

	/* 
	 * Score
	 */
	if(score == DONT_KNOW) {
		return _NOTFOUND;
	} else if(score == DRAW) {
		return 0;
	} else {
		return eval(player,piece,square,score);
	}
}

/*
4 men
*/
DLLExport void CDECL load_egbb(char* path) {
	load_egbb_xxx(path,4194304,LOAD_4MEN);
}
DLLExport int CDECL probe_egbb(int player, int w_king, int b_king,
			    int piece1, int square1,
			    int piece2, int square2
			   ) {
	int piece[MAX_PIECES],square[MAX_PIECES];
	piece[0] =  wking; square[0] = w_king;
	piece[1] =  bking; square[1] = b_king;
	piece[2] = piece1; square[2] = square1;
	piece[3] = piece2; square[3] = square2;
	piece[4] = _EMPTY; square[4] = 0;
	return probe_egbb_xxx(player,piece,square);
}
/*
5 men
*/
DLLExport void CDECL load_egbb_5men(char* path,int cache_size,int load_options) {
        load_egbb_xxx(path,cache_size,load_options);
}
DLLExport int  CDECL probe_egbb_5men(int player, int w_king, int b_king,
			    int piece1, int square1,
			    int piece2, int square2,
				int piece3, int square3
			   ) {
	int piece[MAX_PIECES],square[MAX_PIECES];
	piece[0] =  wking; square[0] = w_king;
	piece[1] =  bking; square[1] = b_king;
	piece[2] = piece1; square[2] = square1;
	piece[3] = piece2; square[3] = square2;
	piece[4] = piece3; square[4] = square3;
	piece[5] = _EMPTY; square[5] = 0;
	return probe_egbb_xxx(player,piece,square);
}
/*
x men
*/
DLLExport void CDECL load_egbb_xmen(char* path,int cache_size,int load_options) {
        load_egbb_xxx(path,cache_size,load_options);
}
DLLExport int  CDECL probe_egbb_xmen(int player, int* piece,int* square) {
	return probe_egbb_xxx(player,piece,square);
}