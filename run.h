// Enable TTY control?
#define TC
// Enable artificial delays?
#define DELAY

// Pass some params to builtins

extern int EXIT;
extern int OUTFD;

// Functions
void run_bg_update(job*,int,int);
void run_bg_supervise(void);
void run_fg_set(job*);
void run(job*);

