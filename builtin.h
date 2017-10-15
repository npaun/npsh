// Lookup modes for builtins
#define BUILTIN_INLINE 0
#define BUILTIN_FORK 1


// Type of a builtin function
typedef void (builtin_t) (int,char**);

// Functions
builtin_t* builtin_lookup(int,char*);
