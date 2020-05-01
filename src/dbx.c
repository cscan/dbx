#include <stdlib.h>
#include <regex.h>
#include <ctype.h>
#include <time.h>
#include "../redismodule.h"
#include "../rmutil/util.h"
#include "../rmutil/strings.h"
#include "../rmutil/vector.h"
#include "../rmutil/test_util.h"

/* Helper function: compiles a regex, or dies complaining. */
int regexCompile(RedisModuleCtx *ctx, regex_t *r, const char *t) {
  int status = regcomp(r, t, REG_EXTENDED | REG_NOSUB | REG_NEWLINE);

  if (status) {
    char rerr[128];
    char err[256];
    regerror(status, r, rerr, 128);
    sprintf(err, "ERR regex compilation failed: %s", rerr);
    RedisModule_ReplyWithError(ctx, err);
    return status;
  }

  return 0;
}

char* toLower(char* s) {
  for(char *p = s; *p; p++)
    *p = tolower(*p);
  return s;
}
char* toUpper(char* s) {
  for(char *p = s; *p; p++)
    *p = toupper(*p);
  return s;
}

int whereRecord(RedisModuleCtx *ctx, RedisModuleString *key, Vector *vWhere) {
  char *field, *w;
  int condition;
  int match = 1;

  // If where statement is defined, get the specified hash content and do comparison
  size_t n = Vector_Size(vWhere);
  if (n == 0) return 1;
  if (n % 3 != 0) return 0;
  for (size_t i = 0; i < n; i += 3) {
    Vector_Get(vWhere, i, &field);
    Vector_Get(vWhere, i+1, &condition);
    Vector_Get(vWhere, i+2, &w);
    if (condition == 7)
      toLower(w);
    if (strlen(w) == 0) return 0;

    RedisModuleCallReply *tags = RedisModule_Call(ctx, "HGET", "sc", key, field);
    if (RedisModule_CallReplyLength(tags) == 0) {
      RedisModule_FreeCallReply(tags);
      return 0;
    }
    RedisModuleString *rms = RedisModule_CreateStringFromCallReply(tags);
    size_t l;
    const char *s = RedisModule_StringPtrLen(rms, &l);
    switch(condition) {
      case 0:
        match = strcmp(s, w) >= 0? 1: 0;
        break;
      case 1:
        match = strcmp(s, w) <= 0? 1: 0;
        break;
      case 2:
      case 3:
        match = strcmp(s, w) != 0? 1: 0;
        break;
      case 4:
        match = strcmp(s, w) > 0? 1: 0;
        break;
      case 5:
        match = strcmp(s, w) < 0? 1: 0;
        break;
      case 6:
        match = strcmp(s, w) == 0? 1: 0;
        break;
      case 7:
        match = strstr(toLower((char*)s), w)? 1: 0;
        break;
    }
    RedisModule_FreeString(ctx, rms);
    RedisModule_FreeCallReply(tags);
    if (match == 0) return 0;
  }
  return match;
}

void showRecord(RedisModuleCtx *ctx, RedisModuleString *key, Vector *vSelect) {
  RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);

  char* field;
  size_t nSelected = Vector_Size(vSelect);
  size_t n = 0;
  for(size_t i = 0; i < nSelected; i++) {
    Vector_Get(vSelect, i, &field);

    // If '*' is specified in selected hash list, display all hashs then
    if (strcmp(field, "*") == 0) {
      RedisModuleCallReply *tags = RedisModule_Call(ctx, "HGETALL", "s", key);
      size_t tf = RedisModule_CallReplyLength(tags);
      if (tf > 0) {
        for(size_t j=0; j<tf; j++) {
          RedisModuleString *rms = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(tags, j));
          RedisModule_ReplyWithString(ctx, rms);
          n++;
          RedisModule_FreeString(ctx, rms);
        }
      }
      RedisModule_FreeCallReply(tags);
    }
    else if (strcmp(field, "rowid()") == 0) {
      RedisModule_ReplyWithSimpleString(ctx, field);
      RedisModule_ReplyWithString(ctx, key);
      n += 2;
    }
    else {
      // Display the hash name and content
      RedisModule_ReplyWithSimpleString(ctx, field);
      RedisModuleCallReply *tags = RedisModule_Call(ctx, "HGET", "sc", key, field);
      if (RedisModule_CallReplyLength(tags) > 0) {
        RedisModuleString *rms = RedisModule_CreateStringFromCallReply(tags);
        RedisModule_ReplyWithString(ctx, rms);
        RedisModule_FreeString(ctx, rms);
      }
      else
        RedisModule_ReplyWithNull(ctx); // If hash is undefined
      n += 2;
      RedisModule_FreeCallReply(tags);
    }
  }
  RedisModule_ReplySetArrayLength(ctx, n);
}

/* Split the string by specified delimilator */
Vector* splitStringByChar(char *s, char* d) {
  size_t cap;
  char *p = s;
  for (cap=1; p[cap]; p[cap]==d[0] ? cap++ : *p++);

  Vector *v = NewVector(void *, cap);
  if (strlen(s) > 0) {
    char *token = strtok(s, d);
    for(int i=0; i<cap; i++) {
      if (token != NULL) Vector_Push(v, token);
      token = strtok(NULL, d);
    }
  }
  return v;
}

Vector* splitWhereString(char *s) {
  Vector *v = NewVector(void *, 16);
  char *token = strtok(s, "&&");
  while (token != NULL) {
    static char chk[8][3] = {">=", "<=", "!=", "<>", ">", "<", "=", "~"};
    char *p = token;
    while(*p++) {
      for(int i=0; i<8; i++) {
        char *c = chk[i];
        if (strncmp(c, p, strlen(c)) == 0) {
          *p = 0;
          p += strlen(c);
          Vector_Push(v, token);
          Vector_Push(v, i);
          Vector_Push(v, p);
          break;
        }
      }
    }
    token = strtok(NULL, "&&");
  }
  return v;
}

int processRecords(RedisModuleCtx *ctx, RedisModuleCallReply *keys, regex_t *r, Vector *vSelect, Vector *vWhere) {
  size_t nKeys = RedisModule_CallReplyLength(keys);
  size_t affected = 0;
  for (size_t i = 0; i < nKeys; i++) {
    RedisModuleString *key = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(keys, i));
    size_t l;
    const char *s = RedisModule_StringPtrLen(key, &l);
    if (!regexec(r, s, 1, NULL, 0)) {
      if (vWhere == NULL || whereRecord(ctx, key, vWhere)) {
        showRecord(ctx, key, vSelect);
        affected++;
      }
    }
    RedisModule_FreeString(ctx, key);
  }
  return affected;
}

/* Create temporary set for sorting */
int buildSetByPattern(RedisModuleCtx *ctx, regex_t *r, char *setName, Vector *vWhere) {
  RedisModule_Call(ctx, "DEL", "c", setName);
  RedisModuleString *scursor = RedisModule_CreateStringFromLongLong(ctx, 0);
  long long lcursor;
  size_t affected = 0;
  do {
    RedisModuleCallReply *rep = RedisModule_Call(ctx, "SCAN", "s", scursor);

    /* Get the current cursor. */
    scursor = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(rep, 0));
    RedisModule_StringToLongLong(scursor, &lcursor);

    /* Filter by pattern matching. */
    RedisModuleCallReply *keys = RedisModule_CallReplyArrayElement(rep, 1);
    size_t nKeys = RedisModule_CallReplyLength(keys);
    for (size_t i = 0; i < nKeys; i++) {
      RedisModuleString *key = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(keys, i));
      size_t l;
      const char *s = RedisModule_StringPtrLen(key, &l);
      if (!regexec(r, s, 1, NULL, 0)) {
        if (vWhere == NULL || whereRecord(ctx, key, vWhere)) {
          RedisModule_Call(ctx, "SADD", "cs", setName, key);
          affected++;
        }
      }
      RedisModule_FreeString(ctx, key);
    }

    RedisModule_FreeCallReply(keys);
    RedisModule_FreeCallReply(rep);
  } while (lcursor);

  return affected;
}

int SelectCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc < 2)
    return RedisModule_WrongArity(ctx);

  // Table
  RedisModuleString *fromKeys;

  // Process the arguments
  size_t plen;
  char s[1024] = "";
  for (int i=1; i<argc; i++) {
    if (strlen(s) > 0) strcat(s, " ");
    const char *temp = RedisModule_StringPtrLen(argv[i], &plen);
    if (strlen(s) + plen > 1024) {
        RedisModule_ReplyWithError(ctx, "arguments are too long");
        return REDISMODULE_ERR;
    }

    if (argc > 2) { // argc > 2 means the arguments are not in quoted. i.e. "..."
      char *p = (char*)temp;
      while (*p++) *p = *p == 32? 7: *p; // Convert all spaces in tabs, then convert back during parsing
    }

    if (strcmp("like", temp) == 0)
      strcat(s, "~");
    else
      strcat(s, temp);
  }

  char *sp = s;
  if (strncmp("select", sp, 6) == 0) sp += 6;

  int step = 0;
  char temp[1024] = "";
  char stmSelect[1024] = "";
  char stmWhere[1024] = "";
  char stmOrder[1024] = "";

  char *token = strtok(sp, " ");
  while (token != NULL) {
    // If it is beginning in single quote, find the end quote in the following tokens
    if (token[0] == 39) {
      strcpy(temp, &token[1]);
      strcat(temp, " ");
      strcat(temp, strtok(NULL, "'"));
      strcpy(token, temp);
    }
    switch(step) {
      case 0:
        if (strcmp("from", token) == 0)
          step = -1;
        else {
          if (strlen(stmSelect) + strlen(token) > 512) {
            RedisModule_ReplyWithError(ctx, "select arguments are too long");
            return REDISMODULE_ERR;
          }
          strcat(stmSelect, token);
        }
        break;
      case -1:
        // parse from clause
        fromKeys = RMUtil_CreateFormattedString(ctx, token);
        step = 2;
        break;
      case 2:
        if (strcmp("where", token) == 0)
          step = -3;
        else if (strcmp("order", token) == 0)
          step = -5;
        else {
          RedisModule_ReplyWithError(ctx, "where or order statement is expected");
          return REDISMODULE_ERR;
        }
        break;
      case -3:
      case 4:
        // parse where clause
        if (strcmp("order", token) == 0)
          step = -5;
        else if (strcmp("and", token) == 0)
          strcat(stmWhere, "&&");
        else {
          if (strlen(stmWhere) + strlen(token) > 512) {
            RedisModule_ReplyWithError(ctx, "where arguments are too long");
            return REDISMODULE_ERR;
          }
          char *p = token;
          while (*p++) *p = *p == 7? 32: *p;
          strcat(stmWhere, token);
          step = 4;
        }
        break;
      case -5:
        if (strcmp("by", token) == 0)
          step = -6;
        else {
          RedisModule_ReplyWithError(ctx, "missing 'by' after order");
          return REDISMODULE_ERR;
        }
        break;
      case -6:
      case 7:
        // parse order clause
        if (strlen(stmOrder) + strlen(token) > 512) {
          RedisModule_ReplyWithError(ctx, "order arguments are too long");
          return REDISMODULE_ERR;
        }
        if (strcmp("desc", token) == 0)
          strcat(stmOrder, "-");
        else
        if (strcmp("asc", token) != 0)
          strcat(stmOrder, token);
        step = 7;
        break;
    }
    token = strtok(NULL, " ");
  }

  if (step <= 0) {
    RedisModule_ReplyWithError(ctx, "parse error");
    return REDISMODULE_ERR;
  }

  Vector *vSelect = splitStringByChar(stmSelect, ",");
  Vector *vWhere = splitWhereString(stmWhere);
  Vector *vOrder = splitStringByChar(stmOrder, ",");

  RedisModule_AutoMemory(ctx);

   /* Convert key to regex */
  const char *pat = RedisModule_StringPtrLen(fromKeys, &plen);
  regex_t regex;
  if (regexCompile(ctx, &regex, pat)) return REDISMODULE_ERR;

  /* Print result in array format */
  RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);

  if (Vector_Size(vOrder) > 0) {
    // temporary set name
    char setName[32];
    sprintf(setName, "__db_tempset_%i", rand());

    char stmt[128];
    RedisModuleCallReply *rep;

    if (buildSetByPattern(ctx, &regex, setName, vWhere) > 0) {
      // Sort the fields under the key and send the resulting array to processRecords module
      char *field;
      Vector_Get(vOrder, 0, &field);
      if (field[strlen(field)-1] == '-') {
        field[strlen(field)-1] = 0;
        sprintf(stmt, "*->%s", field);
        rep = RedisModule_Call(ctx, "SORT", "ccccc", setName, "by", stmt, "desc", "alpha");
      }
      else {
        sprintf(stmt, "*->%s", field);
        rep = RedisModule_Call(ctx, "SORT", "cccc", setName, "by", stmt, "alpha");
      }
      size_t n = processRecords(ctx, rep, &regex, vSelect, NULL);
      RedisModule_FreeCallReply(rep);

      // set number of output
      RedisModule_ReplySetArrayLength(ctx, n);
    }
    else
      RedisModule_ReplySetArrayLength(ctx, 0);

    // Remove the temporary set before leave
    RedisModule_Call(ctx, "DEL", "c", setName);
  }
  else {
    RedisModuleString *scursor = RedisModule_CreateStringFromLongLong(ctx, 0);
    long long lcursor;
    size_t n = 0;
    do {
      RedisModuleCallReply *rep = RedisModule_Call(ctx, "SCAN", "s", scursor);

      /* Get the current cursor. */
      scursor = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(rep, 0));
      RedisModule_StringToLongLong(scursor, &lcursor);

      /* Filter by pattern matching. */
      RedisModuleCallReply *rkeys = RedisModule_CallReplyArrayElement(rep, 1);

      n += processRecords(ctx, rkeys, &regex, vSelect, vWhere);

      RedisModule_FreeCallReply(rep);
    } while (lcursor);

    RedisModule_ReplySetArrayLength(ctx, n);
    RedisModule_FreeString(ctx, scursor);
  }

  RedisModule_FreeString(ctx, fromKeys);
  Vector_Free(vSelect);
  Vector_Free(vWhere);
  Vector_Free(vOrder);

  return REDISMODULE_OK;
}

int InsertCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc < 2)
    return RedisModule_WrongArity(ctx);

  // Table
  RedisModuleString *intoKey;

  // Process the arguments
  size_t plen;
  char s[1024] = "";
  for (int i=1; i<argc; i++) {
    if (strlen(s) > 0) strcat(s, " ");
    const char *temp = RedisModule_StringPtrLen(argv[i], &plen);
    if (strlen(s) + plen > 1024) {
        RedisModule_ReplyWithError(ctx, "arguments are too long");
        return REDISMODULE_ERR;
    }

    if (argc > 2) { // argc > 2 means the arguments are not in quoted. i.e. "..."
      char *p = (char*)temp;
      while (*p++) *p = *p == 32? 7: *p; // Convert all spaces in tabs, then convert back during parsing
    }
    strcat(s, temp);
  }

  char *sp = s;
  if (strncmp("insert", sp, 6) == 0) sp += 6;

  int step = 0;
  char temp[1024] = "";
  char stmField[1024] = "";
  char stmValue[1024] = "";

  char *p;
  char *token = strtok(sp, " ");
  while (token != NULL) {
    if (token[0] == 39) {
      strcpy(temp, &token[1]);
      strcat(temp, " ");
      strcat(temp, strtok(NULL, "'"));
      strcpy(token, temp);
    }
    switch(step) {
      case 0:
        if (strcmp("into", token) == 0)
          step = -1;
        else {
            RedisModule_ReplyWithError(ctx, "into keyword is expected");
            return REDISMODULE_ERR;
        }
        break;
      case -1:
        // parse into clause, assume time+rand always is new key
        intoKey = RMUtil_CreateFormattedString(ctx, "%s:%u-%i", token, (unsigned)time(NULL), rand());
        step = -2;
        break;
      case -2:
        if (token[0] == '(') {
          strcpy(stmField, &token[1]);
          if (token[strlen(token) - 1] == ')') {
            stmField[strlen(stmField) - 1] = 0;
            step = -4;
          }
          else
            step = -3;
        }
        else if (strcmp("values", token) == 0)
          step = -5;
        else {
            RedisModule_ReplyWithError(ctx, "values keyword is expected");
            return REDISMODULE_ERR;
        }
        break;
      case -3:
        if (token[strlen(token) - 1] == ')') {
          token[strlen(token) - 1] = 0;
          strcat(stmField, token);
          step = -4;
        }
        break;
      case -4:
        if (strcmp("values", token) == 0)
          step = -5;
        else {
            RedisModule_ReplyWithError(ctx, "values keyword is expected");
            return REDISMODULE_ERR;
        }
        break;
      case -5:
      case -6:
        p = token;
        while (*p++) *p = *p == 7? 32: *p;
        if (token[0] == '(') {
          strcpy(stmValue, &token[1]);
          if (token[strlen(token) - 1] == ')') {
            stmValue[strlen(stmValue) - 1] = 0;
            step = 7;
          }
          else
            step = -6;
        }
        else if (token[strlen(token) - 1] == ')') {
          token[strlen(token) - 1] = 0;
          strcat(stmValue, token);
          step = 7;
        }
        else
          strcat(stmValue, token);
        break;
      case 7:
        RedisModule_ReplyWithError(ctx, "The end of statement is expected");
        return REDISMODULE_ERR;
        break;
    }
    token = strtok(NULL, " ");
  }

  if (step < 7) {
    RedisModule_ReplyWithError(ctx, "parse error");
    return REDISMODULE_ERR;
  }

  Vector *vField = splitStringByChar(stmField, ",");
  Vector *vValue = splitStringByChar(stmValue, ",");

  if (Vector_Size(vField) != Vector_Size(vValue)) {
    RedisModule_ReplyWithError(ctx, "Number of values does not match");
    return REDISMODULE_ERR;
  }

  RedisModule_AutoMemory(ctx);

  for (size_t i=0; i<Vector_Size(vField); i++) {
    char *field, *value;
    Vector_Get(vField, i, &field);
    Vector_Get(vValue, i, &value);
    RedisModuleCallReply *rep = RedisModule_Call(ctx, "HSET", "scc", intoKey, field, value);
    RedisModule_FreeCallReply(rep);
  }

  RedisModule_ReplyWithString(ctx, intoKey);
  RedisModule_FreeString(ctx, intoKey);
  Vector_Free(vField);
  Vector_Free(vValue);

  return REDISMODULE_OK;
}

int DeleteCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc < 2)
    return RedisModule_WrongArity(ctx);

  // Table
  RedisModuleString *fromKeys;

  // Process the arguments
  size_t plen;
  char s[1024] = "";
  for (int i=1; i<argc; i++) {
    if (strlen(s) > 0) strcat(s, " ");
    const char *temp = RedisModule_StringPtrLen(argv[i], &plen);
    if (strlen(s) + plen > 1024) {
        RedisModule_ReplyWithError(ctx, "arguments are too long");
        return REDISMODULE_ERR;
    }

    if (argc > 2) { // argc > 2 means the arguments are not in quoted. i.e. "..."
      char *p = (char*)temp;
      while (*p++) *p = *p == 32? 7: *p; // Convert all spaces in tabs, then convert back during parsing
    }
    strcat(s, temp);
  }

  char *sp = s;
  if (strncmp("delete", sp, 6) == 0) sp += 6;

  int step = 0;
  char temp[1024] = "";
  char stmWhere[1024] = "";

  char *token = strtok(sp, " ");
  while (token != NULL) {
    // If it is beginning in single quote, find the end quote in the following tokens
    if (token[0] == 39) {
      strcpy(temp, &token[1]);
      strcat(temp, " ");
      strcat(temp, strtok(NULL, "'"));
      strcpy(token, temp);
    }
    switch(step) {
      case 0:
        if (strcmp("from", token) == 0)
          step = -1;
        else {
            RedisModule_ReplyWithError(ctx, "from keyword is expected");
            return REDISMODULE_ERR;
        }
        break;
      case -1:
        // parse from clause
        fromKeys = RMUtil_CreateFormattedString(ctx, token);
        step = 2;
        break;
      case 2:
        if (strcmp("where", token) == 0)
          step = -3;
        else {
          RedisModule_ReplyWithError(ctx, "where statement is expected");
          return REDISMODULE_ERR;
        }
        break;
      case -3:
      case 4:
        // parse where clause
        if (strlen(stmWhere) + strlen(token) > 512) {
          RedisModule_ReplyWithError(ctx, "where arguments are too long");
          return REDISMODULE_ERR;
        }
        if (strcmp("and", token) == 0)
          strcat(stmWhere, "&&");
        else {
          char *p = token;
          while (*p++) *p = *p == 7? 32: *p;
          strcat(stmWhere, token);
          step = 4;
        }
        break;
    }
    token = strtok(NULL, " ");
  }

  if (step <= 0) {
    RedisModule_ReplyWithError(ctx, "parse error");
    return REDISMODULE_ERR;
  }

  Vector *vWhere = splitWhereString(stmWhere);

  RedisModule_AutoMemory(ctx);

  /* Convert key to regex */
  const char *pat = RedisModule_StringPtrLen(fromKeys, &plen);
  regex_t regex;
  if (regexCompile(ctx, &regex, pat)) return REDISMODULE_ERR;

  RedisModuleString *scursor = RedisModule_CreateStringFromLongLong(ctx, 0);
  long long lcursor;
  size_t affected = 0;
  do {
    RedisModuleCallReply *rep = RedisModule_Call(ctx, "SCAN", "s", scursor);

    /* Get the current cursor. */
    scursor = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(rep, 0));
    RedisModule_StringToLongLong(scursor, &lcursor);

    /* Filter by pattern matching. */
    RedisModuleCallReply *keys = RedisModule_CallReplyArrayElement(rep, 1);
    size_t nKeys = RedisModule_CallReplyLength(keys);
    for (size_t i = 0; i < nKeys; i++) {
      RedisModuleString *key = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(keys, i));
      size_t l;
      const char *s = RedisModule_StringPtrLen(key, &l);
      if (!regexec(&regex, s, 1, NULL, 0)) {
        if (vWhere == NULL || whereRecord(ctx, key, vWhere)) {
          RedisModule_Call(ctx, "DEL", "s", key);
          affected++;
        }
      }
      RedisModule_FreeString(ctx, key);
    }

    RedisModule_FreeCallReply(rep);
  } while (lcursor);

  RedisModule_FreeString(ctx, scursor);

  RedisModule_ReplyWithLongLong(ctx, affected);
  RedisModule_FreeString(ctx, fromKeys);
  Vector_Free(vWhere);

  return REDISMODULE_OK;
}

int ExecCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc < 2)
    return RedisModule_WrongArity(ctx);

  size_t plen;
  const char *arg = RedisModule_StringPtrLen(argv[1], &plen);

  if (strncmp(arg, "select", 6) == 0)
    return SelectCommand(ctx, argv, argc);
  else if (strncmp(arg, "insert", 6) == 0)
    return InsertCommand(ctx, argv, argc);
  else if (strncmp(arg, "delete", 6) == 0)
    return DeleteCommand(ctx, argv, argc);
  else {
    RedisModule_ReplyWithError(ctx, "parse error");
    return REDISMODULE_ERR;
  }
}

int RedisModule_OnLoad(RedisModuleCtx *ctx) {

  // Register the module
  if (RedisModule_Init(ctx, "dbx", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  // Register the command
  if (RedisModule_CreateCommand(ctx, "dbx.select", SelectCommand, "readonly", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx, "dbx.insert", InsertCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx, "dbx.delete", DeleteCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  // Register the command
  if (RedisModule_CreateCommand(ctx, "dbx", ExecCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  return REDISMODULE_OK;
}
