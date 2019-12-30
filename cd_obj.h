#ifndef CD_OBJ_H
#define CD_OBJ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct cd_obj_mesh_t {
    char dummy;
    
} cd_obj_scene_t;


typedef bool (*cd_obj_mtllib_handler)(void* userdata, const char* filename, size_t filename_length);

bool cd_obj_parse_mtllib(void*        userdata,
                         const char*  ptr,
                         size_t       size);

cd_obj_scene_t* cd_obj_parse(void*                  userdata,
                             cd_obj_mtllib_handler  mtllib_handler,
                             const char*            ptr,
                             size_t                 size);

#ifdef CD_OBJ_IMPLEMENTATION

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#define CD_OBJ_LOG_ERROR(ctx,...) do { fprintf(stderr,__VA_ARGS__); fputc('\n',stderr); } while(0)

typedef struct {
    float x, y, z;
} cd_obj_vec3f_t;

typedef struct
{
  uint32_t vtx[3];
  uint32_t nrm[3];
  uint32_t tex[3];
  uint32_t smoothingGroup;
  uint32_t object;
  uint32_t color;
} cd_obj_tri_t;

typedef struct
{
  uint32_t vtx[2];
  uint32_t object;
  uint32_t color;
} cd_obj_line_t;

typedef struct cd_obj_object_t
{
  struct cd_obj_object_t* next;
  const char* str;
  uint32_t id;
} cd_obj_object_t;


#define CD_OBJ_BLOCK_SIZE 1024
#define CD_OBJ_DECLARE_BLOCK(type,name)  \
typedef struct cd_obj_block_##name   \
{                                   \
  struct cd_obj_block##name* next;  \
  unsigned fill;                    \
  type data[CD_OBJ_BLOCK_SIZE];     \
} cd_obj_block_##name;


CD_OBJ_DECLARE_BLOCK(cd_obj_vec3f_t,vec3f_t)
CD_OBJ_DECLARE_BLOCK(cd_obj_tri_t,tri_t)
CD_OBJ_DECLARE_BLOCK(cd_obj_line_t,line_t)



typedef struct
{
    struct {
        cd_obj_block_vec3f_t* first, last;
    } vertices;
    struct {
        cd_obj_block_vec3f_t* first, last;
    } normals;
    struct {
        cd_obj_block_vec3f_t* first, last;
    } texcoords;
    struct {
        cd_obj_block_tri_t* first, last;
    } triangles;
    struct {
        cd_obj_block_tri_t* first, last;
    } lines;
    struct {
        cd_obj_object_t* first, last;
    } objects;

    uint32_t line;
    uint32_t currentObject;
    uint32_t currentSmoothingGroup;
    uint32_t currentColor;

    
#if 0
  Logger logger = nullptr;
  Arena arena;
  StringInterning strings;
#endif
    uint32_t vertices_n;
    uint32_t normals_n;
    uint32_t texcoords_n;
    uint32_t triangles_n;
    uint32_t lines_n;
    uint32_t objects_n;

    void* userdata;
    cd_obj_mtllib_handler mtllib_handler;
    const char* begin;
    const char* end;
    const char* ptr;
    
    bool useNormals;
    bool useTexcoords;
    bool useSmoothingGroups;
} cd_obj_context;


static const bool cd_obj_parse_int(cd_obj_context* ctx, int32_t* value)
{
    int32_t sign = 1;
    if (ctx->ptr < ctx->end) {
        if (*ctx->ptr == '-') { sign = -1; ctx->ptr++; }
        else if (*ctx->ptr == '+') ctx->ptr++;
    }
    
    int64_t t = 0;
    for (; ctx->ptr < ctx->end && '0' <= *ctx->ptr && *ctx->ptr <= '9'; ctx->ptr++) {
        if (t < 0x100000000) {
            t = 10 * t + (*ctx->ptr - '0');
        }
    }
    t = sign * t;
    
    if (t < INT32_MIN  || INT32_MAX < t) {
        CD_OBJ_LOG_ERROR(ctx,"Integer out of range of int32_t at line %u", ctx->line);
        return false;
    }
    *value = (int32_t)t;
    return true;
}

static const bool cd_obj_parse_uint(cd_obj_context* ctx, uint32_t* value)
{
    uint64_t t = 0;
    for (; ctx->ptr < ctx->end && '0' <= *ctx->ptr && *ctx->ptr <= '9'; ctx->ptr++) {
        if (t < 0x100000000) {
            t = 10 * t + (*ctx->ptr - '0');
        }
    }
    
    if (UINT32_MAX < t) {
        CD_OBJ_LOG_ERROR(ctx,"Unsigned integer out of range of uint32_t at line %u", ctx->line);
        return false;
    }
    else *value = (uint32_t)t;
    return true;
}


static const bool cd_obj_parse_float(cd_obj_context* ctx, float* value)
{
    // Note: no attempt on correct rounding.

    // read sign
    int32_t sign = 1;
    if (ctx->ptr < ctx->end) {
        if (*ctx->ptr == '-') { sign = -1; ctx->ptr++; }
        else if (*ctx->ptr == '+') ctx->ptr++;
    }
    
    int32_t mantissa = 0;
    int32_t exponent = 0;
    for (; ctx->ptr < ctx->end && '0' <= *ctx->ptr && *ctx->ptr <= '9'; ctx->ptr++) {
        if (mantissa < 100000000) {
            mantissa = 10 * mantissa + (*ctx->ptr - '0');
        }
        else {
            exponent++;
        }
    }
    if (ctx->ptr < ctx->end && *ctx->ptr == '.') {
        ctx->ptr++;
        for (; ctx->ptr < ctx->end && '0' <= *ctx->ptr && *ctx->ptr <= '9'; ctx->ptr++) {
            if (mantissa < 100000000) {
                mantissa = 10 * mantissa + (*ctx->ptr - '0');
                exponent--;
            }
        }
    }
    if (ctx->ptr < ctx->end && *ctx->ptr == 'e') {
        ctx->ptr++;
        int32_t expSign = 1;
        if (ctx->ptr < ctx->end) {
            if (*ctx->ptr == '-') { expSign = -1; ctx->ptr++; }
            else if (*ctx->ptr == '+') ctx->ptr++;
        }
        
        int32_t exp = 0;
        for (; ctx->ptr < ctx->end && '0' <= *ctx->ptr && *ctx->ptr <= '9'; ctx->ptr++) {
            if (exp < 100000000) {
                exp = 10 * exp + (*ctx->ptr - '0');
            }
        }
        
        exponent += expSign * exp;
    }
    *value = (float)(sign*mantissa)*powf(10.f, exponent);
    return true;
}


inline void cd_obj_skip_whitespace(cd_obj_context* ctx)
{
    while (ctx->ptr < ctx->end) {
        char v = *ctx->ptr;
        if(!(v =='\r' || v == ' ' || v == '\f' || v == '\t' || v == '\v')) return;
        ctx->ptr++;
    }
}


#define CD_OBJ_KEYWORD(a,b,c,d) (((uint32_t)(unsigned char)(a))<<24)|(((uint32_t)(unsigned char)(b))<<16)|(((uint32_t)(unsigned char)(c))<<8)|((uint32_t)(unsigned char)(d))

static bool cd_obj_parse_loop(cd_obj_context* ctx)
{
    cd_obj_skip_whitespace(ctx);
    while(ctx->ptr < ctx->end) {
        const char* b = ctx->ptr;

        bool recognized = false;
        if(*ctx->ptr != '#') {
            uint32_t keyword = 0;
            while(ctx->ptr < ctx->end && 'a' <= *ctx->ptr && *ctx->ptr <= 'z') {
                keyword = (keyword << 8) | (unsigned char)*ctx->ptr++;
            }
            size_t l = ctx->ptr - b;
            if(l <= 4) {
                recognized = true;
                switch(keyword) {
                case CD_OBJ_KEYWORD(0,0,0,'v'):
                    //parseVec<3>(&context, context.vertices, context.vertices_n, r, q);
                    break;
                case CD_OBJ_KEYWORD(0,0,'v','n'):
                    //parseVec<3>(&context, context.normals, context.normals_n, r, q);
                    break;
                case CD_OBJ_KEYWORD(0,0,'v','t'):
                    //parseVec<2>(&context, context.texcoords, context.texcoords_n, r, q);
                    break;
                case CD_OBJ_KEYWORD(0,0,0,'f'): // face primitive
                    //parseF(&context, r, q);
                    break;
                case CD_OBJ_KEYWORD(0,0,0,'o'): // o object_name
                    //parseO(&context, r, q);
                    break;
                case CD_OBJ_KEYWORD(0,0,0,'s'): // s group_number
                    //parseS(&context, r, q);
                    break;
                case CD_OBJ_KEYWORD(0,0,0,'l'): // line primitive
                    //parseL(&context, r, q);
                    break;
                case CD_OBJ_KEYWORD(0,0,0,'p'):        // point primitive
                case CD_OBJ_KEYWORD(0,0,0,'g'):        // g group_name1 group_name2
                case CD_OBJ_KEYWORD(0,0,'m','g'):
                case CD_OBJ_KEYWORD(0,0,'v','p'):
                case CD_OBJ_KEYWORD(0,'d','e','g'):
                case CD_OBJ_KEYWORD('b', 'm', 'a', 't'):
                case CD_OBJ_KEYWORD('s', 't', 'e', 'p'):
                case CD_OBJ_KEYWORD('c', 'u', 'r', 'v'):
                case CD_OBJ_KEYWORD('s', 'u', 'r', 'f'):
                case CD_OBJ_KEYWORD('t', 'r', 'i', 'm'):
                case CD_OBJ_KEYWORD('h', 'o', 'l', 'e'):
                case CD_OBJ_KEYWORD(0,0,'s', 'p'):
                case CD_OBJ_KEYWORD(0,'e', 'n', 'd'):
                case CD_OBJ_KEYWORD(0,'c', 'o', 'n'):
                case CD_OBJ_KEYWORD(0,'l', 'o', 'd'):
                    break;
                default:
                    recognized = false;
                    break;
                }
            }
            else {
              if (l == 5 && memcmp(ctx->ptr, "curv2", 5) == 0) recognized = true;
              else if (l == 5 && memcmp(ctx->ptr, "ctech", 5) == 0) recognized = true;
              else if (l == 5 && memcmp(ctx->ptr, "stech", 5) == 0) recognized = true;
              else if (l == 5 && memcmp(ctx->ptr, "bevel", 5) == 0) recognized = true;
              else if (l == 6 && memcmp(ctx->ptr, "cstype", 6) == 0) recognized = true;
              else if (l == 6 && memcmp(ctx->ptr, "maplib", 6) == 0) recognized = true;
              else if (l == 6 && memcmp(ctx->ptr, "usemap", 6) == 0) recognized = true;
              else if (l == 6 && memcmp(ctx->ptr, "usemtl", 6) == 0) {
                //parseUseMtl(&context, r, q);
                recognized = true;
              }
              else if (l == 6 && memcmp(ctx->ptr, "mtllib", 6) == 0) recognized = true;
              else if (l == 8 && memcmp(ctx->ptr, "c_interp", 8) == 0) recognized = true;
              else if (l == 8 && memcmp(ctx->ptr, "d_interp", 8) == 0) recognized = true;
              else if (l == 9 && memcmp(ctx->ptr, "trace_obj", 9) == 0) recognized = true;
              else if (l == 10 && memcmp(ctx->ptr, "shadow_obj", 10) == 0) recognized = true;
            }

            if (recognized == false) {
                CD_OBJ_LOG_ERROR(ctx, "Unrecognized keyword '%.*s' at line %d", (int)l, ctx->ptr, ctx->line);
            }
        }

        // Gobble rest of line including newline
        while (ctx->ptr < ctx->end) {
            if(*ctx->ptr++ == '\n') break;
        }
        ctx->line++;
        cd_obj_skip_whitespace(ctx);
        assert(b < ctx->ptr && "No progress");
    }
    return true;
}


cd_obj_scene_t* cd_obj_parse(void*                  userdata,
                             cd_obj_mtllib_handler  mtllib_handler,
                             const char*            ptr,
                             size_t                 size)
{
    cd_obj_context ctx = {
        .line = 1,
        .userdata = userdata,
        .mtllib_handler = mtllib_handler,
        .begin = ptr,
        .end = ptr + size,
        .ptr = ptr
    };
    if(cd_obj_parse_loop(&ctx)) {
        
    }

#if 0
    
    auto * p = (const char*)(ptr);
    auto * end = p + size;
    while (p < end) {
      context.line++;
      p = skipSpacing(p, end);        // skip inital spaces on line
      auto *q = endOfine(p, end);     // get end of line, before newline etc.

      if (p < q) {
        auto * r = skipNonSpacing(p, end);  // Find keyword
        auto l = r - p;                     // length of keyword

        bool recognized = false;
        if (l <= 4) {
          unsigned keyword = key(p, l);
          switch (keyword) {
          }
        }
      }

      p = skipNewLine(q, end);
    }


    auto * mesh = new Mesh();

    {
      BBox3f bbox = createEmptyBBox3f();
      unsigned o = 0;
      mesh->vtxCount = context.vertices_n;
      mesh->vtx = (Vec3f*)mesh->arena.alloc(sizeof(Vec3f)*mesh->vtxCount);

      for (auto * block = context.vertices.first; block; block = block->next) {

        for (unsigned i = 0; i < block->fill; i++) {
          engulf(bbox, block->data[i]);
          mesh->vtx[o++] = block->data[i];
        }
      }
      assert(o == mesh->vtxCount);
      mesh->bbox = bbox;
    }

    {
      mesh->triCount = context.triangles_n;
      mesh->triVtxIx = (uint32_t*)mesh->arena.alloc(sizeof(uint32_t) * 3 * mesh->triCount);
      mesh->TriObjIx = (uint32_t*)mesh->arena.alloc(sizeof(uint32_t) * mesh->triCount);
      mesh->triColor = (uint32_t*)mesh->arena.alloc(sizeof(uint32_t) * mesh->triCount);

      unsigned o = 0;
      for (auto * block = context.triangles.first; block; block = block->next) {
        for (unsigned i = 0; i < block->fill; i++) {
          for (unsigned k = 0; k < 3; k++) {
            auto ix = block->data[i].vtx[k];
            assert(0 <= ix && ix < context.vertices_n);
            mesh->triVtxIx[3 * o + k] = ix;
          }
          mesh->TriObjIx[o] = block->data[i].object;
          mesh->triColor[o] = block->data[i].color;
          o++;
        }
      }
      assert(o == mesh->triCount);
    }

    if(context.lines_n) {
      mesh->lineCount = context.lines_n;
      mesh->lineVtxIx = (uint32_t*)mesh->arena.alloc(sizeof(uint32_t) * 3 * mesh->lineCount);
      mesh->lineColor = (uint32_t*)mesh->arena.alloc(sizeof(uint32_t) * mesh->lineCount);
      unsigned o = 0;
      for (auto * block = context.lines.first; block; block = block->next) {
        for (unsigned i = 0; i < block->fill; i++) {
          for (unsigned k = 0; k < 2; k++) {
            auto ix = block->data[i].vtx[k];
            assert(0 <= ix && ix < context.vertices_n);
            mesh->lineVtxIx[2 * o + k] = ix;
          }
          mesh->lineColor[o] = block->data[i].color;
          o++;
        }
      }
    }

    if (context.useSmoothingGroups) {
      mesh->triSmoothGroupIx = (uint32_t*)mesh->arena.alloc(sizeof(uint32_t)*mesh->triCount);

      unsigned o = 0;
      for (auto * block = context.triangles.first; block; block = block->next) {
        for (unsigned i = 0; i < block->fill; i++) {
          mesh->triSmoothGroupIx[o] = block->data[i].smoothingGroup;
          o++;
        }
      }
      assert(o == mesh->triCount);

    }

    {
      unsigned o = 0;
      mesh->obj_n = context.objects_n;
      mesh->obj = (const char**)mesh->arena.alloc(sizeof(const char*)*mesh->obj_n);
      for (auto * obj = context.objects.first; obj; obj = obj->next) {
        mesh->obj[o++] = mesh->strings.intern(obj->str);
      }
      assert(o == mesh->obj_n);
    }

    if (context.useNormals) {
      unsigned o;

      o = 0;
      mesh->nrmCount = context.normals_n;
      mesh->nrm = (Vec3f*)mesh->arena.alloc(sizeof(Vec3f)*mesh->nrmCount);
      for (auto * block = context.normals.first; block; block = block->next) {
        for (unsigned i = 0; i < block->fill; i++) {
          mesh->nrm[o++] = block->data[i];
        }
      }
      assert(o == mesh->nrmCount);

      o = 0;
      mesh->triNrmIx = (uint32_t*)mesh->arena.alloc(sizeof(uint32_t) * 3 * mesh->triCount);
      for (auto * block = context.triangles.first; block; block = block->next) {
        for (unsigned i = 0; i < block->fill; i++) {
          for (unsigned k = 0; k < 3; k++) {
            auto ix = block->data[i].nrm[k];
            assert(0 <= ix && ix < context.normals_n);
            mesh->triNrmIx[o++] = ix;
          }
        }
      }
      assert(o == 3 * mesh->triCount);
    }

    if (context.useTexcoords) {

      unsigned o;

      o = 0;
      mesh->texCount = context.texcoords_n;
      mesh->tex = (Vec2f*)mesh->arena.alloc(sizeof(Vec2f)*mesh->texCount);
      for (auto * block = context.texcoords.first; block; block = block->next) {
        for (unsigned i = 0; i < block->fill; i++) {
          mesh->tex[o++] = block->data[i];
        }
      }
      assert(o == mesh->texCount);

      o = 0;
      mesh->triTexIx = (uint32_t*)mesh->arena.alloc(sizeof(uint32_t) * 3 * mesh->triCount);
      for (auto * block = context.triangles.first; block; block = block->next) {
        for (unsigned i = 0; i < block->fill; i++) {
          for (unsigned k = 0; k < 3; k++) {
            auto ix = block->data[i].tex[k];
            assert(0 <= ix && ix < context.texcoords_n);
            mesh->triTexIx[o++] = ix;
          }
        }
      }
      assert(o == 3 * mesh->triCount);
    }


    logger(0, "readObj parsed %d lines, Vn=%d, Nn=%d, Tn=%d, tris=%d",
           context.line, context.vertices_n, context.normals_n, context.texcoords_n, context.triangles_n);
#endif
    
    
    return NULL;
}



#if 0
#include <cassert>
#include "Common.h"
#include "LinAlg.h"
#include "LinAlgOps.h"
#include "Mesh.h"

namespace {





  const char* endOfine(const char* p, const char* end)
  {
    while (p < end && (*p != '#' && *p != '\n' && *p != '\r'))  p++;
    return p;
  }

  const char* skipNewLine(const char* p, const char* end)
  {
    if (p < end && *p == '#') {
      while (p < end && (*p != '\n' && *p != '\r'))  p++;
    }
    if (p < end && (*p == '\r'))  p++;
    if (p < end && (*p == '\n'))  p++;
    return p;
  }

  const char* skipSpacing(const char* p, const char* end)
  {
    while (p < end && (*p=='\r' || *p == ' ' || *p == '\f' || *p == '\t' || *p == '\v')) p++;
    return p;
  }

  const char* skipNonSpacing(const char* p, const char* end)
  {
    while (p < end && (*p != '\r' && *p != ' ' && *p != '\f' && *p != '\t' && *p != '\v')) p++;
    return p;
  }

  




  template<unsigned d, typename Element>
  void parseVec(Context* context, ListHeader<Block<Element>>& V, uint32_t& N,  const char * a, const char* b)
  {
    if (V.empty() || V.last->capacity <= V.last->fill + 1) {
      V.append(context->arena.alloc<Block<Element>>());
    }
    assert(V.last->fill < V.last->capacity);
    auto * v = V.last->data[V.last->fill++].data;
    unsigned i = 0;
    for (; a < b && i < d; i++) {
      a = skipSpacing(a, b);
      auto * q = a;
      a = parseFloat(context, v[i], a, b);
    }
    for (; i < d; i++) v[i] = 0.f;
    N++;
  }

  const char* parseIndex(Context* context, uint32_t& vi, uint32_t& ti, uint32_t& ni, const char* a, const char* b)
  {
    ti = ~0u;
    ni = ~0u;

    int ix;
    a = skipSpacing(a, b);
    a = parseInt(context, ix, a, b);
    if (ix < 0) ix += context->vertices_n;
    else --ix;
    if (ix < 0 || context->vertices_n <= uint32_t(ix)) {
      context->logger(2, "Illegal vertex index %d at line %d.", ix, context->line);
      return a;
    }
    vi = ix;

    if (a < b && *a == '/') {
      a++;
      if (a < b && *a != '/' && *a != ' ') {
        a = parseInt(context, ix, a, b);
        if (ix < 0) ix += context->texcoords_n;
        else --ix;
        if (ix < 0 || context->texcoords_n <= uint32_t(ix)) {
          context->logger(2, "Illegal texcoord index %d at line %d.", ix, context->line);
          return a;
        }
        ti = ix;
        context->useTexcoords = true;
      }
      if (a < b && *a == '/' && *a != ' ') {
        a++;
        a = parseInt(context, ix, a, b);
        if (ix < 0) ix += context->normals_n;
        else --ix;
        if (ix < 0 || context->normals_n <= uint32_t(ix)) {
          context->logger(2, "Illegal normal index %d at line %d.", ix, context->line);
          return a;
        }
        ni = ix;
        context->useNormals = true;
      }
    }
    return a;
  }

  void parseF(Context* context, const char* a, const char* b)
  {
    Triangle t;
    t.smoothingGroup = context->currentSmoothingGroup;
    t.object = context->currentObject;
    t.color = context->currentColor;

    unsigned k = 0;
    for (; a < b && k < 3; k++) {
      uint32_t vi, ti, ni;
      a = parseIndex(context, vi, ti, ni, a, b);
      t.vtx[k] = vi;
      t.tex[k] = ti;
      t.nrm[k] = ni;
    }
    if (k == 3) {
      auto & T = context->triangles;
      if(T.empty() || T.last->capacity <= T.last->fill + 1) {
        T.append(context->arena.alloc<Block<Triangle>>());
      }
      T.last->data[T.last->fill++] = t;
      context->triangles_n++;
    }
    else {
      context->logger(2, "Skipped malformed triangle at line %d", context->line);
    }

    while (true) {
      a = skipSpacing(a, b);
      if (a < b && (('0' <= *a && *a <= '9') || *a == '-' || *a == '+')) {
        uint32_t vi, ti, ni;
        a = parseIndex(context, vi, ti, ni, a, b);

        Triangle r;
        r.smoothingGroup = context->currentSmoothingGroup;
        r.object = context->currentObject;
        r.color = context->currentColor;

        r.vtx[0] = t.vtx[0];
        r.tex[0] = t.tex[0];
        r.nrm[0] = t.nrm[0];
        r.vtx[1] = t.vtx[2];
        r.tex[1] = t.tex[2];
        r.nrm[1] = t.nrm[2];
        r.vtx[2] = vi;
        r.tex[2] = ti;
        r.nrm[2] = ni;
        auto & T = context->triangles;
        if (T.empty() || T.last->capacity <= T.last->fill + 1) {
          T.append(context->arena.alloc<Block<Triangle>>());
        }
        T.last->data[T.last->fill++] = r;
        context->triangles_n++;
        t = r;
      }
      else {
        break;
      }
    }
  }

  void parseL(Context* context, const char* a, const char* b)
  {
    int ix;

    Line l;
    l.object = context->currentObject;
    l.color = context->currentColor;

    unsigned k = 0;
    for (; a < b && k < 2; k++) {
      a = skipSpacing(a, b);
      a = parseInt(context, ix, a, b);
      if (ix < 0) ix += context->vertices_n;
      else --ix;
      if (ix < 0 || context->vertices_n <= uint32_t(ix)) {
        context->logger(2, "Illegal vertex index %d at line %d.", ix, context->line);
        return;
      }
      l.vtx[k] = ix;
    }
    if (k == 2) {
      auto & T = context->lines;
      if (T.empty() || T.last->capacity <= T.last->fill + 1) {
        T.append(context->arena.alloc<Block<Line>>());
      }
      T.last->data[T.last->fill++] = l;
      context->lines_n++;
    }
    else {
      context->logger(2, "Skipped malformed line at line %d", context->line);
    }
  }


  void parseS(Context* context, const char* a, const char* b)
  {
    a = skipSpacing(a, b);
    auto * m = skipNonSpacing(a, b);
    if (m - a == 3) {
      char t[3] = { a[0], a[1], a[2] };
      if (t[0] == 'o' && t[1] == 'f' && t[2] == 'f') {
        context->currentSmoothingGroup = 0;
        return;
      }
    }
    a = parseUInt(context, context->currentSmoothingGroup, a, b);
    if (a != m) {
      context->logger(2, "Malformed smoothing group id at line %d", context->line);
      context->currentSmoothingGroup = 0;
    }
    if (context->currentSmoothingGroup) context->useSmoothingGroups = true;
  }

  void parseUseMtl(Context* context, const char* a, const char* b)
  {
    a = skipSpacing(a, b);
    auto * m = skipNonSpacing(a, b);
    if ((m - a == 8) && a[0] == '0' && (a[1] == 'x' || a[1] == 'X')) {
      auto aa = a + 2;
      uint32_t color = 0;
      for (unsigned i = 0; i < 6; i++) {
        auto c = aa[i];
        color = color << 4;
        if (c < '0') goto notHexInline;
        if (c <= '9') { color |= c - '0'; continue; }
        if (c < 'A') goto notHexInline;
        if (c <= 'F') { color |= c - 'A' + 10; continue; }
        if (c < 'a') goto notHexInline;
        if (c <= 'f') { color |= c - 'a' + 10; continue; }
        else goto notHexInline;
      }
      context->currentColor = color;
      return;
    }
  notHexInline:
    context->currentColor = 0x888888;
  }


  void parseO(Context* context, const char* a, const char* b)
  {
    a = skipSpacing(a, b);
    auto * m = skipNonSpacing(a, b);

    auto * obj = context->arena.alloc<Object>();
    obj->str = context->strings.intern(a, m);;
    obj->id = 1 + context->objects_n++;
    context->objects.append(obj);
    context->currentObject = obj->id;
  }

}
#endif



#endif  // CD_OBJ_IMPLEMENTATION

#ifdef __cplusplus
}   // extern "C"
#endif

#endif  // CD_OBJ_H
