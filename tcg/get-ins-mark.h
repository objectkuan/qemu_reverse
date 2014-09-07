#ifndef GET_INS_MARK_H
#define GET_INS_MARK_H

#define MAX_BBEXEC_LEN 4096

typedef struct BBExec {
    FILE* fp;
    tcg_target_long pc;
    unsigned int len;
} BBExec;

BBExec bbexec;

static inline void init_bbexec(BBExec *bbexec) {
    bbexec->pc = 0x0;
    bbexec->len = 0;
    bbexec->fp = fopen("output","wb");
}

static void insert_bbexec(BBExec *bbexec, tcg_target_long pc) {
    if (bbexec->pc == pc) {
        bbexec->len++;	
	return;
    }
    /*printf("Last: 0x%lx x %u\n", (unsigned long) bbexec->pc, bbexec->len);*/
    fwrite(&(bbexec->pc), sizeof(bbexec->pc), 1, bbexec->fp);
    fflush(bbexec->fp);
    bbexec->pc = pc;
    bbexec->len = 1;
}


#endif
