#ifdef __cplusplus
extern "C" {
#endif

/* Prototypes for __malloc_hook, __free_hook */
#include <malloc.h>
#include <stdio.h>
#include <pthread.h>

/* Prototypes for our hooks.  */
static void my_init_hook (void);
static void *my_malloc_hook (size_t, const void *);
static void my_free_hook (void*, const void *);

static void* (*old_malloc_hook)( size_t, const void *) = 0;
static void (*old_free_hook)( void*, const void *) = 0;

/* Override initializing hook from the C library. */
void (*__MALLOC_HOOK_VOLATILE __malloc_initialize_hook)() = my_init_hook;

/* Synchronization */
static pthread_mutex_t my_malloc_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Failure counter */
#define MY_MALLOC_FAILURE_LIMIT 1000
static int my_failure_counter = 0;

static void my_init_hook (void)
{
	fprintf( stdout, "+++ CALLED my_init_hook\n");
	pthread_mutex_init( &my_malloc_mutex, 0);
	old_malloc_hook = __malloc_hook;
	old_free_hook = __free_hook;
	__malloc_hook = my_malloc_hook;
	__free_hook = my_free_hook;
}

static void* my_malloc_hook (size_t size, const void *caller)
{
	void *result;
	/* Restore all old hooks */
	pthread_mutex_lock( &my_malloc_mutex);
	__malloc_hook = old_malloc_hook;
	__free_hook = old_free_hook;
	if (++my_failure_counter == MY_MALLOC_FAILURE_LIMIT)
	{
		/* Return null, when failure count limit reached */
		my_failure_counter = 0;
		result = 0;
	}
	else
	{
		/* Call glibc malloc */
		result = malloc (size);
	}
	/* Restore our own hooks */
	__malloc_hook = my_malloc_hook;
	__free_hook = my_free_hook;
	pthread_mutex_unlock( &my_malloc_mutex);
	return result;
}

static void my_free_hook (void *ptr, const void *caller)
{
	/* Restore all old hooks */
	pthread_mutex_lock( &my_malloc_mutex);
	__malloc_hook = old_malloc_hook;
	__free_hook = old_free_hook;
	/* Call glibc free */
	free( ptr);
	/* Restore our own hooks */
	__malloc_hook = my_malloc_hook;
	__free_hook = my_free_hook;
	pthread_mutex_unlock( &my_malloc_mutex);
}

#ifdef __cplusplus
}
#endif
