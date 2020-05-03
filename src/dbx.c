#include <stdlib.h>
#include <regex.h>
#include <ctype.h>
#include <time.h>
#include "../redismodule.h"
#include "../rmutil/util.h"
#include "../rmutil/strings.h"
#include "../rmutil/vector.h"
#include "../rmutil/test_util.h"

static int rn;

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

char* trim(char* s, char t) {
  char* p = s;
  if (p[strlen(s)-1] == t) p[strlen(s)-1] = 0;
  if (p[0] == t) p++;
  return p;
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

const char* RedisModule_StringToChar(RedisModuleString *s) {
  size_t l;
  return RedisModule_StringPtrLen(s, &l);
}

char* VectorGetString(Vector *v, size_t i) {
  char *value;
  Vector_Get(v, i, &value);
  return value;
}

int whereRecord(RedisModuleCtx *ctx, RedisModuleString *key, Vector *vWhere) {
  //char *field;
  char *w;
  int condition;
  int match = 1;

  // If where statement is defined, get the specified hash content and do comparison
  size_t n = Vector_Size(vWhere);
  if (n == 0) return 1;
  if (n % 3 != 0) return 0;
  for (size_t i = 0; i < n; i += 3) {
    // Vector_Get(vWhere, i, &field);
    Vector_Get(vWhere, i+1, &condition);
    Vector_Get(vWhere, i+2, &w);
    if (condition == 7)
      toLower(w);
    if (strlen(w) == 0) return 0;

    RedisModuleCallReply *tags = RedisModule_Call(ctx, "HGET", "sc", key, VectorGetString(vWhere, i));
    if (RedisModule_CallReplyLength(tags) == 0) {
      RedisModule_FreeCallReply(tags);
      return 0;
    }
    RedisModuleString *rms = RedisModule_CreateStringFromCallReply(tags);
    const char *s = RedisModule_StringToChar(rms);
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

    // If '*' is specified in selected hash list, display all hashes then
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

void intoRecord(RedisModuleCtx *ctx, RedisModuleString *key, Vector *vSelect, char *intoKey) {
  char* field;
  size_t nSelected = Vector_Size(vSelect);
  char newkey[64];

  sprintf(newkey, "%s:%u-%i", intoKey, (unsigned)time(NULL), rn++);
  for(size_t i = 0; i < nSelected; i++) {
    Vector_Get(vSelect, i, &field);

    // If '*' is specified in selected hash list, display all hashes then
    if (strcmp(field, "*") == 0) {
      RedisModuleCallReply *tags = RedisModule_Call(ctx, "HGETALL", "s", key);
      size_t tf = RedisModule_CallReplyLength(tags);
      if (tf > 0) {
        for(size_t j=0; j<tf; j+=2) {
          RedisModuleString *rms1 = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(tags, j));
          RedisModuleString *rms2 = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(tags, j+1));
          RedisModule_Call(ctx, "HSET", "css", newkey, rms1, rms2);
          RedisModule_FreeString(ctx, rms1);
          RedisModule_FreeString(ctx, rms2);
        }
      }
      RedisModule_FreeCallReply(tags);
    }
    else {
      // Display the hash name and content
      RedisModuleCallReply *tags = RedisModule_Call(ctx, "HGET", "sc", key, field);
      if (RedisModule_CallReplyLength(tags) > 0) {
        RedisModuleString *rms = RedisModule_CreateStringFromCallReply(tags);
        RedisModule_Call(ctx, "HSET", "ccs", newkey, field, rms);
        RedisModule_FreeString(ctx, rms);
      }
      else
        RedisModule_Call(ctx, "HSET", "ccc", newkey, field, "");
      RedisModule_FreeCallReply(tags);
    }
  }
  RedisModule_ReplyWithSimpleString(ctx, newkey);
}

void intoCSV(RedisModuleCtx *ctx, RedisModuleString *key, Vector *vSelect, char *filename) {
  char* field;
  size_t nSelected = Vector_Size(vSelect);
  char line[1024];

  FILE *fp = fopen(filename, "a");
  strcpy(line, "");
  for(size_t i = 0; i < nSelected; i++) {
    Vector_Get(vSelect, i, &field);
    // If '*' is specified in selected hash list, display all hashes then
    if (strcmp(field, "*") == 0) {
      RedisModuleCallReply *tags = RedisModule_Call(ctx, "HGETALL", "s", key);
      size_t tf = RedisModule_CallReplyLength(tags);
      if (tf > 0) {
        for(size_t j=0; j<tf; j+=2) {
          // RedisModuleString *rms1 = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(tags, j));
          RedisModuleString *rms2 = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(tags, j+1));
          if (strlen(line) > 0) strcat(line, ",");
          strcat(line, RedisModule_StringToChar(rms2));
          // RedisModule_FreeString(ctx, rms1);
          RedisModule_FreeString(ctx, rms2);
        }
      }
      RedisModule_FreeCallReply(tags);
    }
    else {
      if (strlen(line) > 0) strcat(line, ",");
      RedisModuleCallReply *tags = RedisModule_Call(ctx, "HGET", "sc", key, field);
      if (RedisModule_CallReplyLength(tags) > 0) {
        RedisModuleString *rms = RedisModule_CreateStringFromCallReply(tags);
        strcat(line, RedisModule_StringToChar(rms));
        RedisModule_FreeString(ctx, rms);
      }
      RedisModule_FreeCallReply(tags);
    }
  }
  RedisModule_ReplyWithSimpleString(ctx, line);
  fprintf(fp, "%s\n", line);
  fclose(fp);
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

size_t processRecords(RedisModuleCtx *ctx, RedisModuleCallReply *keys, regex_t *r, Vector *vSelect, Vector *vWhere, long *top, char *intoKey, char *csvFile) {
  size_t nKeys = RedisModule_CallReplyLength(keys);
  size_t affected = 0;
  for (size_t i = 0; i < nKeys; i++) {
    RedisModuleString *key = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(keys, i));
    const char *s = RedisModule_StringToChar(key);
    if (!regexec(r, s, 1, NULL, 0)) {
      if (vWhere == NULL || whereRecord(ctx, key, vWhere)) {
        if (strlen(csvFile) > 0)
          intoCSV(ctx, key, vSelect, csvFile);
        else if (strlen(intoKey) > 0)
          intoRecord(ctx, key, vSelect, intoKey);
        else
          showRecord(ctx, key, vSelect);
        affected++;
        (*top)--;
      }
    }
    RedisModule_FreeString(ctx, key);
    if (*top == 0) return affected;
  }
  return affected;
}

/* Create temporary set for sorting */
size_t buildSetByPattern(RedisModuleCtx *ctx, regex_t *r, char *setName, Vector *vWhere) {
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
      const char *s = RedisModule_StringToChar(key);
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
  RedisModule_AutoMemory(ctx);

  if (argc < 2)
    return RedisModule_WrongArity(ctx);

  // Table
  long top = -1;
  RedisModuleString *fromKeys;
  char intoKey[32] = "";
  char csvFile[128] = "";

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
        if (strcmp("top", token) == 0) {
          step = -2;
          break;
        }
        step = -1;
      case -1:
        if (strlen(token) > 512) {
          RedisModule_ReplyWithError(ctx, "select arguments are too long");
          return REDISMODULE_ERR;
        }
        strcat(stmSelect, token);
        step = -3;
        break;
      case -2:
        top = atol(token);
        step = -1;
        break;
      case -3:
        if (strcmp("into", token) == 0)
          step = -4;
        else if (strcmp("from", token) == 0)
          step = -6;
        else {
          if (strlen(stmSelect) + strlen(token) > 512) {
            RedisModule_ReplyWithError(ctx, "select arguments are too long");
            return REDISMODULE_ERR;
          }
          strcat(stmSelect, token);
        }
        break;
      case -4:
        // parse into clause, assume time+rand always is new key
        if (strcmp("csv", token) == 0)
          step = -45;
        else {
          strcpy(intoKey, token);
          step = -5;
        }
        break;
      case -45:
        strcpy(csvFile, token);
        step = -5;
        break;
      case -5:
        if (strcmp("from", token) == 0)
          step = -6;
        else {
          RedisModule_ReplyWithError(ctx, "from keyword is expected");
          return REDISMODULE_ERR;
        }
        break;
      case -6:
        // parse from clause
        fromKeys = RMUtil_CreateFormattedString(ctx, token);
        step = 7;
        break;
      case 7:
        if (strcmp("where", token) == 0)
          step = -8;
        else if (strcmp("order", token) == 0)
          step = -10;
        break;
      case -8:
      case 9:
        // parse where clause
        if (strcmp("order", token) == 0)
          step = -10;
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
          step = 9;
        }
        break;
      case -10:
        if (strcmp("by", token) == 0)
          step = -11;
        else {
          RedisModule_ReplyWithError(ctx, "missing 'by' after order");
          return REDISMODULE_ERR;
        }
        break;
      case -11:
      case 12:
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
        step = 12;
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

   /* Convert key to regex */
  const char *pat = RedisModule_StringToChar(fromKeys);
  regex_t regex;
  if (regexCompile(ctx, &regex, pat)) return REDISMODULE_ERR;

  /* Print result in array format */
  RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);

  if (Vector_Size(vOrder) > 0) {
    // temporary set name
    char setName[32];
    sprintf(setName, "__db_tempset_%i", rn++);

    RedisModuleCallReply *rep;

    if (buildSetByPattern(ctx, &regex, setName, vWhere) > 0) {
      char *field;
      int nSortField = Vector_Size(vOrder);
      int cap = 3 * nSortField + 2;

      RedisModuleString *param[cap];
      param[0] = RedisModule_CreateString(ctx, setName, strlen(setName));

      for(int sf = 0; sf < nSortField; sf++) {
        Vector_Get(vOrder, sf, &field);
        if (field[strlen(field)-1] == '-') {
          field[strlen(field)-1] = 0;
          param[3+sf*3] = RedisModule_CreateString(ctx, "desc", 4);
        }
        else
          param[3+sf*3] = RedisModule_CreateString(ctx, "asc", 3);
        param[1+sf*3] = RedisModule_CreateString(ctx, "by", 2);
        param[2+sf*3] = RedisModule_CreateStringPrintf(ctx, "*->%s", field, 3 + strlen(field));
      }
      param[cap-1] = RedisModule_CreateString(ctx, "alpha", 5);
      rep = RedisModule_Call(ctx, "SORT", "v", &param, cap);

      for(int i = 0; i < cap; i++)
        RedisModule_FreeString(ctx, param[i]);

      size_t n = processRecords(ctx, rep, &regex, vSelect, NULL, &top, intoKey, csvFile);

      RedisModule_FreeCallReply(rep);
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
      n += processRecords(ctx, rkeys, &regex, vSelect, vWhere, &top, intoKey, csvFile);

      RedisModule_FreeCallReply(rkeys);
      RedisModule_FreeCallReply(rep);
      if (top == 0) break;
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
  RedisModule_AutoMemory(ctx);

  if (argc < 2)
    return RedisModule_WrongArity(ctx);

  // Table
  char intoKey[128] = "";
  RedisModuleString *fromCSV = NULL;

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
        strcpy(intoKey, token);
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
        else if (strcmp("from", token) == 0)
          step = -7;
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
        else if (strcmp("from", token) == 0)
          step = -7;
        else {
            RedisModule_ReplyWithError(ctx, "values or from keyword is expected");
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
            step = 8;
          }
          else
            step = -6;
        }
        else if (token[strlen(token) - 1] == ')') {
          token[strlen(token) - 1] = 0;
          strcat(stmValue, token);
          step = 8;
        }
        else
          strcat(stmValue, token);
        break;
      case -7:
        fromCSV = RedisModule_CreateString(ctx, token, strlen(token));
        step = 8;
        break;
      case 8:
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

  if (fromCSV != NULL) {
    const char *filename = RedisModule_StringToChar(fromCSV);
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
      RedisModule_ReplyWithError(ctx, "File does not exist");
      return REDISMODULE_ERR;
    }
    char line[1024];
    char *value, *field;
    size_t n = 0;
    RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);

    while(fgets(line, 1024, fp) != NULL) {
      if (line[strlen(line)-1] == 10) line[strlen(line)-1] = 0;
      if (line[strlen(line)-1] == 13) line[strlen(line)-1] = 0;

      value = strtok(line, ",");
      if (Vector_Size(vField) == 0) {
        while (value != NULL) {
          if (strlen(stmField) > 0) strcat(stmField, ",");
          strcat(stmField, trim(value, '"'));
          value = strtok(NULL, ",");
        }
        if (vField) Vector_Free(vField);
        vField = splitStringByChar(stmField, ",");
        continue;
      }

      RedisModuleString *key = RedisModule_CreateStringPrintf(ctx, "%s:%u-%i", intoKey, (unsigned)time(NULL), rn++);
      for (size_t i=0; i<Vector_Size(vField); i++) {
        if (value == NULL) {
          RedisModule_ReplyWithError(ctx, "Number of values does not match");
          return REDISMODULE_ERR;
        }
        Vector_Get(vField, i, &field);
        RedisModuleCallReply *rep = RedisModule_Call(ctx, "HSET", "scc", key, field, trim(value, '"'));
        RedisModule_FreeCallReply(rep);
        value = strtok(NULL, ",");
      }
      n++;
      RedisModule_ReplyWithString(ctx, key);
      RedisModule_FreeString(ctx, key);
    }
    fclose(fp);
    RedisModule_ReplySetArrayLength(ctx, n);
  }
  else {
    if (Vector_Size(vField) != Vector_Size(vValue)) {
      RedisModule_ReplyWithError(ctx, "Number of values does not match");
      return REDISMODULE_ERR;
    }

    char *field, *value;
    size_t n = 0;
    RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);

    RedisModuleString *key = RMUtil_CreateFormattedString(ctx, "%s:%u-%i", intoKey, (unsigned)time(NULL), rn++);
    for (size_t i=0; i<Vector_Size(vField); i++) {
      Vector_Get(vField, i, &field);
      Vector_Get(vValue, i, &value);
      RedisModuleCallReply *rep = RedisModule_Call(ctx, "HSET", "scc", key, field, trim(value, '"'));
      RedisModule_FreeCallReply(rep);
    }
    n++;
    RedisModule_ReplyWithString(ctx, key);
    RedisModule_FreeString(ctx, key);
    RedisModule_ReplySetArrayLength(ctx, n);
  }
  if (vField) Vector_Free(vField);
  if (vValue) Vector_Free(vValue);

  return REDISMODULE_OK;
}

int DeleteCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  RedisModule_AutoMemory(ctx);

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

  /* Convert key to regex */
  const char *pat = RedisModule_StringToChar(fromKeys);
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
      const char *s = RedisModule_StringToChar(key);
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

  const char *arg = RedisModule_StringToChar(argv[1]);

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

  rn = rand();

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
