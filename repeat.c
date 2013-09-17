/*
 * repeat
 * Nathan Forbes
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RP_PROGRAM_NAME         "repeat"
#define RP_VERSION               "0.0.1"
#define RP_PATH_KEY               "PATH"
#define RP_EXECV_ARGV_SIZE           256
#define RP_INTERVAL_TYPE_SECONDS       0
#define RP_INTERVAL_TYPE_MINUTES       1
#define RP_INTERVAL_TYPE_HOURS         2
#define RP_INTERVAL_TYPE_DAYS          3

typedef unsigned char bool;
#define FALSE ((bool) 0)
#define TRUE  ((bool) 1)

extern char **environ;

const char *program_name;
const char *path_value;

long double        wait_value        = 0.0;
int                interval_type     = -1;
bool               unlimited_repeats = TRUE;
unsigned long long repeat_count      = 0;

struct
{
  char *exec;
  char *argv[RP_EXECV_ARGV_SIZE];
} command;

static void
rp_set_program_name (char *argv0)
{
  char *x;

  if (!argv0 || !*argv0)
  {
    program_name = RP_PROGRAM_NAME;
    return;
  }

  x = strrchr (argv0, '/');
  if (x && *x && x[1])
  {
    program_name = ++x;
    return;
  }
  program_name = argv0;
}

static bool
rp_streq (const char *s1, const char *s2)
{
  size_t n;

  n = strlen (s1);
  if ((n == strlen (s2)) && (memcmp (s1, s2, n) == 0))
    return TRUE;
  return FALSE;
}

static bool
rp_strsw (const char *s, const char *p)
{
  size_t n;

  n = strlen (p);
  if ((strlen (s) >= n) && (memcmp (s, p, n) == 0))
    return TRUE;
  return FALSE;
}

static void
rp_perr (const char *s, ...)
{
  size_t x;
  bool fmt;
  va_list ap;

  fputs (program_name, stderr);
  fputs (": error: ", stderr);

  fmt = FALSE;
  for (x = 0; s[x]; ++x)
  {
    if (s[x] == '%')
    {
      fmt = TRUE;
      break;
    }
  }

  if (!fmt)
    fputs (s, stderr);
  else
  {
    va_start (ap, s);
    vfprintf (stderr, s, ap);
    va_end (ap);
  }
  fputc ('\n', stderr);
}

static void
rp_usage (bool err)
{
  fprintf ((!err) ? stdout : stderr,
      "Usage: %s [-[smhd] N] [-n N] [COMMAND]\n",
      program_name);
}

static void
rp_help (void)
{
  rp_usage (FALSE);
  fputs ("Options:\n"
      "  -s N, --seconds=N  Wait N seconds between COMMAND executions\n"
      "  -m N, --minutes=N  Wait N minutes between COMMAND executions\n"
      "  -h N, --hours=N    Wait N hours between COMMAND executions\n"
      "  -d N, --days=N     Wait N days between COMMAND executions\n"
      "  -n N, --repeats=N  Execute COMMAND a total of N times\n"
      "  -h, --help         Print this help text and exit\n"
      "  -v, --version      Print version information and exit\n",
      stdout);
  exit (EXIT_SUCCESS);
}

static void
rp_version (void)
{
  fputs (RP_PROGRAM_NAME " " RP_VERSION "\n"
      "Written by Nathan Forbes\n",
      stdout);
  exit (EXIT_SUCCESS);
}

static long double
rp_strtod (const char *s)
{
  int sign;
  size_t x;
  long double ip;
  long double fp;
  long double fd;
  long double ret;

  ret = 0.0;
  if (s && *s)
  {
    sign = 1;
    ip = 0.0;
    fp = 0.0;
    fd = 1.0;
    x = 0;
    while (isspace (s[x]))
      x++;
    if (s[x] == '-')
      sign = -1;
    while (s[x] != '.' && s[x])
    {
      if (isdigit (s[x]))
      {
        ip = ip * 10 + s[x++] - '0';
        continue;
      }
      x++;
    }
    if (s[x] == '.')
    {
      x++;
      while (s[x])
      {
        if (isdigit (s[x]))
        {
          fd *= 0.1;
          fp += (s[x] - '0') * fd;
        }
        x++;
      }
    }
    ret = (ip + fp) * sign;
  }
  return ret;
}

static unsigned long long
rp_strtoull (const char *s)
{
  size_t x;
  unsigned long long ig;
  unsigned long long ret;

  ret = 0LLU;
  if (s && *s)
  {
    ig = 0LLU;
    x = 0;
    while (isspace (s[x]))
      x++;
    while (s[x])
    {
      if (isdigit (s[x]))
      {
        ig = ig * 10LLU + s[x++] - '0';
        continue;
      }
      x++;
    }
    ret = ig;
  }
  return ret;
}

static void
rp_set_path_value (void)
{
  size_t x;
  size_t n_key;

  n_key = strlen (RP_PATH_KEY);
  for (x = 0; environ[x]; ++x)
  {
    if (rp_strsw (environ[x], RP_PATH_KEY) && (environ[x][n_key] == '='))
    {
      path_value = environ[x] + (n_key + 1);
      return;
    }
  }
  path_value = NULL;
}

static char *
rp_mkabspath (const char *p)
{
  size_t i;
  size_t j;
  size_t pos;
  size_t x;
  size_t n;
  bool complete;
  char *ret;

  n = strlen (p);
  if (*p == '/')
  {
    if (access (p, X_OK) == 0)
    {
      ret = strndup (p, n);
      if (!ret)
      {
        rp_perr ("failed to duplicate string \"%s\"", p);
        return NULL;
      }
      return ret;
    }
    rp_perr ("external program `%s' does not exist", p);
    return NULL;
  }

  x = 0;
  pos = 0;

  for (i = 0; path_value[i]; ++i)
  {
    if (path_value[i] == ':' || !path_value[i + 1])
    {
      if (!pos)
        x = i - 1;
      else if ((i - pos) > x)
        x = (i - pos) - 1;
      pos = i - 1;
    }
  }

  x += n + 1;
  pos = 0;
  complete = FALSE;

  char buffer[x];

  for (i = 0; path_value[i]; ++i)
  {
    if (path_value[i] == ':')
      complete = TRUE;
    else
      buffer[pos++] = path_value[i];
    if (!path_value[i + 1] && !complete)
      complete = TRUE;
    if (complete)
    {
      buffer[pos++] = '/';
      for (j = 0; p[j]; ++j)
        buffer[pos++] = p[j];
      buffer[pos] = '\0';
      if (access (buffer, X_OK) == 0)
      {
        ret = strndup (buffer, pos);
        if (!ret)
        {
          rp_perr ("failed to duplicate string \"%s\"", buffer);
          return NULL;
        }
        return ret;
      }
      pos = 0;
      complete = FALSE;
    }
  }

  rp_perr ("could not find external program `%s' in system path", p);
  return NULL;
}

static void
rp_parse_opts (char **v)
{
  size_t x;
  size_t command_argv_pos;
  char *o;

  rp_set_program_name (v[0]);
  command_argv_pos = 0;
  command.exec = NULL;
  command.argv[0] = NULL;

  o = NULL;
  for (x = 1; v[x]; ++x)
  {
    if (!command.exec)
    {
      if (rp_streq (v[x], "-h") || rp_streq (v[x], "--help"))
        rp_help ();
      else if (rp_streq (v[x], "-v") || rp_streq (v[x], "--version"))
        rp_version ();
      else if (rp_streq (v[x], "-s") || rp_streq (v[x], "--seconds"))
      {
        if (!v[x + 1] || v[x + 1][0] == '-')
        {
          rp_perr ("`%s' requires an argument", v[x]);
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_SECONDS;
        o = v[++x];
      }
      else if (rp_streq (v[x], "-m") || rp_streq (v[x], "--minutes"))
      {
        if (!v[x + 1] || v[x + 1][0] == '-')
        {
          rp_perr ("`%s' requires an argument", v[x]);
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_MINUTES;
        o = v[++x];
      }
      else if (rp_streq (v[x], "-h") || rp_streq (v[x], "--hours"))
      {
        if (!v[x + 1] || v[x + 1][0] == '-')
        {
          rp_perr ("`%s' requires an argument", v[x]);
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_HOURS;
        o = v[++x];
      }
      else if (rp_streq (v[x], "-d") || rp_streq (v[x], "--days"))
      {
        if (!v[x + 1] || v[x + 1][0] == '-')
        {
          rp_perr ("`%s' requires an argument", v[x]);
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_DAYS;
        o = v[++x];
      }
      else if (rp_streq (v[x], "-n") || rp_streq (v[x], "--repeats"))
      {
        if (!v[x + 1] || v[x + 1][0] == '-')
        {
          rp_perr ("`%s' requires an argument", v[x]);
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        unlimited_repeats = FALSE;
        repeat_count = rp_strtoull (v[++x]);
      }
      else if (rp_strsw (v[x], "-s"))
      {
        if (!v[x][2])
        {
          rp_perr ("`-s' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_SECONDS;
        o = v[x] + 2;
      }
      else if (rp_strsw (v[x], "-m"))
      {
        if (!v[x][2])
        {
          rp_perr ("`-m' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_MINUTES;
        o = v[x] + 2;
      }
      else if (rp_strsw (v[x], "-h"))
      {
        if (!v[x][2])
        {
          rp_perr ("`-h' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_HOURS;
        o = v[x] + 2;
      }
      else if (rp_strsw (v[x], "-d"))
      {
        if (!v[x][2])
        {
          rp_perr ("`-d' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_DAYS;
        o = v[x] + 2;
      }
      else if (rp_strsw (v[x], "-n"))
      {
        if (!v[x][2])
        {
          rp_perr ("`-n' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        unlimited_repeats = FALSE;
        repeat_count = rp_strtoull (v[x] + 2);
      }
      else if (rp_strsw (v[x], "--seconds="))
      {
        o = strchr (v[x], '=');
        ++o;
        if (!o || !*o)
        {
          rp_perr ("`--seconds' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_SECONDS;
      }
      else if (rp_strsw (v[x], "--minutes="))
      {
        o = strchr (v[x], '=');
        ++o;
        if (!o || !*o)
        {
          rp_perr ("`--minutes' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_MINUTES;
      }
      else if (rp_strsw (v[x], "--hours="))
      {
        o = strchr (v[x], '=');
        ++o;
        if (!o || !*o)
        {
          rp_perr ("`--hours' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_HOURS;
      }
      else if (rp_strsw (v[x], "--days="))
      {
        o = strchr (v[x], '=');
        ++o;
        if (!o || !*o)
        {
          rp_perr ("`--days' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        interval_type = RP_INTERVAL_TYPE_DAYS;
      }
      else if (rp_strsw (v[x], "--repeats="))
      {
        o = strchr (v[x], '=');
        ++o;
        if (!o || !*o)
        {
          rp_perr ("`--repeats' requires an argument");
          rp_usage (TRUE);
          exit (EXIT_FAILURE);
        }
        unlimited_repeats = FALSE;
        repeat_count = rp_strtoull (o);
      }
      else if (!command.exec && !command.argv[0])
      {
        command.exec = rp_mkabspath (v[x]);
        if (!command.exec)
          exit (EXIT_FAILURE);
        command.argv[command_argv_pos++] = command.exec;
      }
    }
    else
      command.argv[command_argv_pos++] = v[x];
  }

  if (interval_type == -1)
  {
    rp_perr ("no time interval was given");
    rp_usage (TRUE);
    exit (EXIT_FAILURE);
  }

  wait_value = rp_strtod (o);

  if (!command.exec)
  {
    rp_perr ("no command was given");
    rp_usage (TRUE);
    exit (EXIT_FAILURE);
  }
}

static void
rp_exec (void)
{
  int status;
  int child_status;
  pid_t w;
  pid_t child;

  child = fork ();
  if (child == -1)
    rp_perr ("failed to execute command (%s)", strerror (errno));
  else if (child == 0)
  {
    child_status = EXIT_SUCCESS;
    if (execv (command.exec, command.argv) == -1)
    {
      child_status = errno;
      rp_perr ("execv failed (%s)", strerror (child_status));
    }
    _exit (child_status);
  }
  else
  {
    do
    {
      w = waitpid (child, &status, WUNTRACED | WCONTINUED);
      if (w == -1)
        rp_perr ("waitpid failed (%s)", strerror (errno));
    } while (!WIFEXITED (status) && !WIFSIGNALED (status));
  }
}

static bool
rp_istime (long double secs_elapsed)
{
  if ((interval_type == RP_INTERVAL_TYPE_SECONDS) &&
      (secs_elapsed >= wait_value))
    return TRUE;
  else if ((interval_type == RP_INTERVAL_TYPE_MINUTES) &&
           ((secs_elapsed / 60) >= wait_value))
    return TRUE;
  else if ((interval_type == RP_INTERVAL_TYPE_HOURS) &&
           ((secs_elapsed / 3600) >= wait_value))
    return TRUE;
  else if ((interval_type == RP_INTERVAL_TYPE_DAYS) &&
           ((secs_elapsed / 86400) >= wait_value))
    return TRUE;
  return FALSE;
}

static void
rp_run (void)
{
  unsigned long long n;
  long double t_now;
  long double t_last;

  n = 0;
  t_now = ((long double) time (NULL));
  while (TRUE)
  {
    t_last = ((long double) time (NULL));
    if (rp_istime (t_last - t_now))
    {
      rp_exec ();
      t_now = t_last;
      if (!unlimited_repeats && (++n == repeat_count))
        break;
    }
  }
}

static void
rp_cleanup (void)
{
  if (command.exec)
    free (command.exec);
}

int
main (int argc, char **argv)
{
  atexit (rp_cleanup);

  rp_set_path_value ();
  if (path_value == NULL)
  {
    rp_perr ("failed find value of PATH environment variable");
    exit (EXIT_FAILURE);
  }

  rp_parse_opts (argv);
  rp_run ();
  exit (EXIT_SUCCESS);
  return 0;
}

