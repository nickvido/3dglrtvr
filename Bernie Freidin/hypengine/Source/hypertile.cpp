// **************************************************************************************
// **************************************************************************************
// *** HyperTile.cpp (HypEngine)
// *** Copyright (c) 1999-2000 Bernie Freidin
// *** Ported to OpenGL on 2-6-00 
// *** (3Dfx Glide code is still here, but commented-out)
// **************************************************************************************
// **************************************************************************************

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <gl/glut.h>
#include "vector.h"
#include "hypertile.h"

static int WAIT_FOR_CLOCK = 1;
static int MAIN__STOP = 0;
static int TILES_HIT = 0;

#define FLOOR_TEX_ID 1 // 1
#define WALL_TEX_ID 2 // 5
#define CORNER_TEX_ID 2 // 5
#define CEIL_TEX_ID 3

int MAIN__texcount = 4;
unsigned int MAIN__texid[10];
char MAIN__texnames[][200] = {
	"Textures/floor.tga", // floor
	"Textures/wall.tga", // wall,corner
	"Textures/wall.tga", // ceiling
	""
};

static void MAIN__LoadTGA(char *fpath)
{
	FILE *tga = fopen(fpath, "rb");
	if(!tga)
	{
		fprintf(stderr, "\n\ncan't find required TGA (%s)\n", fpath);
		fprintf(stderr, "PRESS RETURN TO EXIT\n");
		char wait[200];
		gets(wait);
		exit(-1);
	}
	// ****************************
	//   2  bytes offset = N
	//   10 bytes of crap
	//   2  bytes = WIDTH
	//   2  bytes = HEIGHT
	//   2  bytes = BPP (24)
	//   N  bytes of crap..
	//   PIXEL DATA FOLLOWS (24bpp)
	// ****************************
	
	short offset, width, height;
	fread(&offset, sizeof(short), 1, tga);
	fseek(tga, 10, SEEK_CUR);
	fread(&width,  sizeof(short), 1, tga);
	fread(&height, sizeof(short), 1, tga);
	fseek(tga, offset+2, SEEK_CUR);
	
	char *rgb = (char*)malloc(3*width*height);
	fread(rgb, 3, width*height, tga);
	fclose(tga);
	
	// Swap red and blue values
	
	for(int i = 0; i < width*height; i++)
	{
		char tmp = rgb[i*3];
		rgb[i*3] = rgb[i*3+2];
		rgb[i*3+2] = tmp;
	}
	gluBuild2DMipmaps(GL_TEXTURE_2D, 3, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    
	free(rgb);
}

static void MAIN__LoadAlphaMask(int size)
{
	char *rgb = (char*)malloc(3*size*size);
	
	for(int y = 0; y < size; y++)
	{
		for(int x = 0; x < size; x++)
		{
			vec2 v = vec2((float)(x-size/2),
			              (float)(y-size/2));
			
			v /= (float)(size/2);
			
			float fog  = 1.f - (float)pow(v*v, 3.f);
			if(fog < 0.f) fog = 0.f;
			if(fog > 1.f) fog = 1.f;
			int fogi = (int)(fog * 255.f);
			
			rgb[x*3 + y*size*3 + 0] = fogi;
			rgb[x*3 + y*size*3 + 1] = fogi;
			rgb[x*3 + y*size*3 + 2] = fogi;
		}
	}
	gluBuild2DMipmaps(GL_TEXTURE_2D, 3, size, size, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    free(rgb);
}

static void MAIN__TrackFPS(void)
{
	static double sum = 0.f;
	static double num = 0.f;
	static int clk_count = 1;
	static clock_t clk1;
	static clock_t clk2;
	
	clk1 = clk2;
	clk2 = clock();
	sum += (double)(clk2-clk1);
	num += 1.f;
	
	if(MAIN__STOP)
	{
	//	FILE *fps_out = fopen("fps.txt", "w");
	//	if(!fps_out) return;
	//	double fps = (double)CLOCKS_PER_SEC * num / sum;
	//	fprintf(fps_out, "AVERAGE FPS= %lf\n", fps);
	//	fclose(fps_out);
		exit(0);
	}
}

static void MAIN__RenderScene(void)
{
}

// *********************************************************
// Backface rejection improves vertex stability by balancing
// the recursion tree. However, it also occasionally omits a
// tile when rendering near the border. This should be fixed
// at some point..
// 
// (note - this is NOT triangle backface rejection!)
// *********************************************************

#define BACKFACE_REJECT 1

// **************************************************************************************
// **************************************************************************************
// *** Structures
// **************************************************************************************
// **************************************************************************************

struct vertex_t
{
	vec3 v; // viewspace coords (x,y,z=1)
	vec3 c; // color value (r,g,b)
	float a;
	vec2 t; // texture coords (s,t)
};

#define OBJ_FLAG_PLAYER 0x001
#define OBJ_FLAG_GHOST  0x002
#define OBJ_FLAG_MOVING 0x004
#define OBJ_FLAG_CAMERA 0x008

struct tile_t;
struct obj_t
{
	int    flags;
	float  r;     // object radius
	float  r_max; // object radius (target)
	mat3   m;     // object transform
	mat3   m_inv; // inverse transform
	tile_t *tile; // parent tile
	obj_t  *next; // next object in tile
	int    verts;
	vec3   color1;
	vec3   color2;
};

struct mesh_t // (432 bytes)
{
	tile_t *tile;
	vec2   v[49]; // mesh verts (poincare)
	mat3   m;
	vec3   color;
};

struct tile_t // (36 bytes)
{
	int    flags; // 4 bits WALL, 4 bits CORNER
	int    frame_rendered;
	int    frame_visited;
	tile_t *next[4];
	obj_t  *obj;
	mesh_t *mesh;
};

// **************************************************************************************
// **************************************************************************************
// *** Defines
// **************************************************************************************
// **************************************************************************************

//Link: http://teach.belmont.edu/ht/applet/htbu.html

#define MAX_WORLD_OBJECTS     4000
#define MAX_ONSCREEN_OBJECTS   500
#define MAX_WORLD_TILES      30000
#define MAX_ONSCREEN_TILES    1000

#define RENDER 1
#if RENDER
/* - 3Dfx Glide Macros
#define RenderPoly(v,c) \
	grDrawVertexArrayContiguous(GR_POLYGON, c, v, sizeof(vertex_t))
#define RenderTriFan(v,c) \
	grDrawVertexArrayContiguous(GR_TRIANGLE_FAN, c, v, sizeof(vertex_t))
#define RenderTriStrip(v,c) \
	grDrawVertexArrayContiguous(GR_TRIANGLE_STRIP, c, v, sizeof(vertex_t))
#define RenderWireframe(v,c) \
	grDrawVertexArrayContiguous(GR_LINE_STRIP, c, v, sizeof(vertex_t)); \
	grDrawVertexArrayContiguous(GR_LINE_STRIP, c/2, v, sizeof(vertex_t)*2)
#define RenderPoint(v) grDrawPoint(v)
*/
// OpenGL Macros

static void RenderPoly(vertex_t *v, int c)
{
	glBegin(GL_POLYGON);
	
	for(int i = 0; i < c; i++)
	{
		glColor3fv((GLfloat*)&v[i].c);
		glTexCoord2fv((GLfloat*)&v[i].t);
		glVertex3fv((GLfloat*)&v[i].v);
	}
	glEnd();
}

static void RenderTriFan(vertex_t *v, int c)
{
	glBegin(GL_TRIANGLE_FAN);
	
	for(int i = 0; i < c; i++)
	{
		glColor3fv((GLfloat*)&v[i].c);
		glTexCoord2fv((GLfloat*)&v[i].t);
		glVertex3fv((GLfloat*)&v[i].v);
	}
	glEnd();
}

static void RenderTriStrip(vertex_t *v, int c)
{
	glBegin(GL_TRIANGLE_STRIP);
	
	for(int i = 0; i < c; i++)
	{
		glColor3fv((GLfloat*)&v[i].c);
		glTexCoord2fv((GLfloat*)&v[i].t);
		glVertex3fv((GLfloat*)&v[i].v);
	}
	glEnd();
}

#define RenderWireframe(v,c) // not used
#define RenderPoint(v) // not used

#else
#define RenderPoly(v,c)
#define RenderTriFan(v,c)
#define RenderTriStrip(v,c)
#define RenderWireframe(v,c)
#define RenderPoint(v)
#endif

/* - 3Dfx Glide
int Render_LoadTexture(char *fn)
{
	GrTexInfo info;
	Gu3dfInfo info2;
	
	long startaddr = grTexMinAddress(GR_TMU0);
	
	// download from disk
	
	gu3dfGetInfo(fn, &info2); info2.data = malloc(info2.mem_required);
	gu3dfLoad   (fn, &info2);
	
	// setup
	
	info.smallLodLog2    = info2.header.small_lod;
	info.largeLodLog2    = info2.header.large_lod;
	info.aspectRatioLog2 = info2.header.aspect_ratio;
	info.format          = info2.header.format;
	info.data            = info2.data;
	
	// upload to hardware
	
	grTexDownloadMipMap(GR_TMU0, startaddr, GR_MIPMAPLEVELMASK_BOTH, &info);
	grTexSource        (GR_TMU0, startaddr, GR_MIPMAPLEVELMASK_BOTH, &info);
	
	free(info2.data);
	
	return 0; // ok
}
*/

// **************************************************************************************
// **************************************************************************************
// *** Global Variables and Macros
// **************************************************************************************
// **************************************************************************************

#define TILE_FLOOR_FLAG 0x0100
#define TILE_FREE_FLAG  0x0200

#define SetWall(tile,k)     (tile)->flags |=  ( 1<<(k))
#define SetCorner(tile,k)   (tile)->flags |=  (16<<(k))
#define SetFloor(tile)      (tile)->flags |= TILE_FLOOR_FLAG
#define SetFree(tile)       (tile)->flags |= TILE_FREE_FLAG
#define ClearWall(tile,k)   (tile)->flags &= ~( 1<<(k))
#define ClearCorner(tile,k) (tile)->flags &= ~(16<<(k))
#define ClearFloor(tile)    (tile)->flags &= ~TILE_FLOOR_FLAG
#define ClearFree(tile)     (tile)->flags &= ~TILE_FREE_FLAG
#define IsWall(tile,k)     ((tile)->flags &   ( 1<<(k)))
#define IsCorner(tile,k)   ((tile)->flags &   (16<<(k)))
#define IsFloor(tile)      ((tile)->flags &   TILE_FLOOR_FLAG)
#define IsFree(tile)       ((tile)->flags &   TILE_FREE_FLAG)

static obj_t  ObjectList[MAX_WORLD_OBJECTS];
static tile_t TileList[MAX_WORLD_TILES];
static mesh_t MeshList[MAX_ONSCREEN_TILES];
static int    ObjectCount = 0;
static int    TileCount   = 0;
static int    MeshCount   = 0;

static float TileLength   = 0.485898000000000f; // Euclidean
static float TileAngle    = 1.256637061435917f; // 2Pi/5
static float ProjectionScale;
static float ProjectionMorph;
static int   CurrentFrame = 1;
static float LimR;

static mat3 MESH__M[16]; // Tile-to-tile traversal transformations
static vec2 MESH__T[49]; // Texture coordinates
static vec2 MESH__V[49]; // Untransformed tile vertices

// **************************************************************************************
// **************************************************************************************
// *** Private Function Prototypes
// **************************************************************************************
// **************************************************************************************

static tile_t *AddTile(tile_t *next);
static int     FindIndex(tile_t *to, tile_t *from);
static void    CurlLeft(tile_t *tile);
static void    CurlRight(tile_t *tile);

static int IntersectCS(float r, vec2 &a, vec2 &b);

static void DrawFloor(tile_t *tile, float height);
static void DrawWall(tile_t *tile, int k, float height);
static void DrawCorner(tile_t *tile, int k, float height);
static void DrawObjects(tile_t *tile);
static void DrawScene(obj_t *obj);

static void VisitTile(tile_t *tile);
static void PlaceObj(obj_t *obj, tile_t *tile);
static void UpdateObj(obj_t *obj, mat3 &m);
static int  CollideObjNode(obj_t *obj);

static int  GetSwapSync(void);

// **************************************************************************************
// **************************************************************************************
// *** Functions For Generating Tiling Topology
// **************************************************************************************
// **************************************************************************************

static tile_t *AddTile(tile_t *next)
{
	tile_t *tile = &TileList[TileCount++];
	
	tile->frame_rendered = 0;
	tile->frame_visited  = 0;
	tile->next[0]        = next;
	tile->next[1]        = NULL;
	tile->next[2]        = NULL;
	tile->next[3]        = NULL;
	tile->obj            = NULL;
	
	SetWall(tile, 0);
	SetWall(tile, 1);
	SetWall(tile, 2);
	SetWall(tile, 3);
	SetFloor(tile);
	SetFree(tile);
	
	return tile;
}

static int FindIndex(tile_t *to, tile_t *from)
{
	if(from->next[0] == to) return 0;
	if(from->next[1] == to) return 1;
	if(from->next[2] == to) return 2;
	if(from->next[3] == to) return 3;
	return -1;
}

static void CurlLeft(tile_t *tile)
{
	tile_t *save = tile;
	int i = 0, count = 1;
	
	while(tile->next[i])
	{
		tile_t *next = tile->next[i];
		i = (FindIndex(tile, next) + 1)&3;
		tile = next;
		count++;
	}
	if(count >= 5)
	{
		tile->next[i] = save;
		save->next[3] = tile;
	}
}

static void CurlRight(tile_t *tile)
{
	tile_t *save = tile;
	int i = 0, count = 1;
	
	while(tile->next[i])
	{
		tile_t *next = tile->next[i];
		i = (FindIndex(tile, next) - 1)&3;
		tile = next;
		count++;
	}
	if(count >= 5)
	{
		tile->next[i] = save;
		save->next[1] = tile;
	}
}

static int IntersectCS(float r, vec2 &a, vec2 &b)
{
	// **********************************************
	// Returns 1 if circle and line segment intersect
	// **********************************************
	
	float tmp1, tmp2;
	vec2 n;
	
	r*=r;
	
	if(a*a < r || b*b < r) return 1;
	n = vec2(a.y - b.y, b.x - a.x);
	tmp1 = a*n;
	if(tmp1*tmp1 >= (n*n)*r) return 0;
	tmp1 = a.x*n.y - a.y*n.x;
	tmp2 = b.x*n.y - b.y*n.x;
	return (tmp1 > 0.f) != (tmp2 > 0.f);
}

static vec2 HProject(vec2 &v)
{
	float  q = ProjectionMorph * (float)sqrt(1.f - v*v);
	return v * ProjectionScale / (1.f + q);
}

static void UpdateMesh(mesh_t *mesh)
{
#define UpdateMeshVert(i, table) \
	v[table[i]] = HProject((mesh->m*MESH__V[table[i]]))
#define CopyMeshVert(i, j, table) \
	v[table[i]] = next->mesh->v[table[j]]
	
	tile_t *tile = mesh->tile;
	vec2   *v    = mesh->v;
	
	static char outer_edge_table[]   = {1,2,3,4,5,13,20,27,34,41,47,
	                                    46,45,44,43,35,28,21,14,7};
	static char inner_edge_table[]   = {9,10,11,19,26,33,39,38,37,
	                                    29,22,15};
	static char outer_corner_table[] = {0,6,48,42};
	static char inner_corner_table[] = {8,12,40,36};
	static char inner_vert_table[]   = {16,17,18,23,24,25,30,31,32};
	
	int i, k, k1, k2;
	
	for(i = 0; i < 9; i++)
	{
		UpdateMeshVert(i, inner_vert_table);
	}
	for(k = 0; k < 4; k++)
	{
		tile_t *next = tile->next[k];
		
		// ****************************
		// Update/copy OUTER EDGE verts
		// ****************************
		
		if(next && next->frame_rendered == CurrentFrame)
		{
			k1 = 5*k;
			k2 = 5*FindIndex(tile, next) + 4;
			
			for(i = 0; i < 5; i++)
			{
				CopyMeshVert(k1 + i, k2 - i, outer_edge_table);
			}
		}
		else
		{
			k1 = 5*k;
			
			for(i = 1; i < 4; i++)
			{
				UpdateMeshVert(k1 + i, outer_edge_table);
			}
			if(IsCorner(tile, k) || (IsWall(tile, (k-1)&3) &&
			                        !IsWall(tile, k)))
			{
				UpdateMeshVert(k1, outer_edge_table);
			}
			if(IsCorner(tile, (k+1)&3) || (IsWall(tile, (k+1)&3) &&
			                              !IsWall(tile, k)))
			{
				UpdateMeshVert(k1 + 4, outer_edge_table);
			}	  
		}
		// ******************************
		// Update/copy OUTER CORNER verts
		// ******************************
		
		if(next && next->frame_rendered == CurrentFrame)
		{
			k2 = (FindIndex(tile, next) + 1)&3;
			
			CopyMeshVert(k, k2, outer_corner_table);
		}
		else
		{
			next = tile->next[(k-1)&3];
			
			if(next && next->frame_rendered == CurrentFrame)
			{
				k2 = FindIndex(tile, next);
				
				CopyMeshVert(k, k2, outer_corner_table);
			}
			else
			{
				UpdateMeshVert(k, outer_corner_table);
			}
		}
		// ***********************
		// Update INNER EDGE verts
		// ***********************
		
		k1 = 3*k;
		
		if(IsWall(tile, k))
		{
			for(i = 0; i < 3; i++)
			{
				UpdateMeshVert(k1 + i, inner_edge_table);
			}
		}
		// *************************
		// Update INNER CORNER verts
		// *************************
		
		if(IsWall(tile, k) && IsWall(tile, (k-1)&3))
		{
			UpdateMeshVert(k, inner_corner_table);
		}
	}
#undef UpdateMeshVert
#undef CopyMeshVert
}

static void DrawFloor(tile_t *tile, float height)
{
	mesh_t *mesh = tile->mesh;
	
	int i, j;
	
	if(height == 1.f)
		glBindTexture(GL_TEXTURE_2D, MAIN__texid[FLOOR_TEX_ID]);
	else
		glBindTexture(GL_TEXTURE_2D, MAIN__texid[CEIL_TEX_ID]);
	
	// ************
	// Compute fade
	// ************
	
//	mesh->color = vec3(.5f, .5f, .5f);
	mesh->color = vec3(.0f, .0f, .0f);
	if(height == 1.f)
	{
		float fade;
		
		if(tile->frame_visited < 1)
		{
			fade = 0.f;
		}
		else
		{
			int frames = CurrentFrame - tile->frame_visited;
			if(frames > 120) fade = .166f;
			else fade = .166f + .633f*(float)(120-frames)/120.f;
		}
		mesh->color = vec3(fade, fade, fade);
	}
	// ***************************
	// Adjust tri-strip parameters
	// ***************************
	
	int index_i[5] = {0, 2, 3, 4, 6};
	int index_j[5] = {0, 14, 21, 28, 42};
	int index_n[4] = {0, 1, 2, 3};
	int count_n[4] = {10, 10, 10, 10};
	
	if(IsWall  (tile, 0)) index_j[0] += 7;
	if(IsWall  (tile, 2)) index_j[4] -= 7;
	if(IsWall  (tile, 3)) index_i[0] += 1;
	if(IsWall  (tile, 1)) index_i[4] -= 1;
	if(IsCorner(tile, 0)) count_n[0] -= 2, index_n[0] += 2;
	if(IsCorner(tile, 1)) count_n[0] -= 2;
	if(IsCorner(tile, 3)) count_n[3] -= 2, index_n[3] += 2;
	if(IsCorner(tile, 2)) count_n[3] -= 2;
	
	// *****************
	// Render tri-strips
	// *****************
	
	vertex_t v[13];
	
	vec2 *v_src = mesh->v + index_j[0];
	vec2 *t_src = MESH__T + index_j[0];
	
	for(i = 0; i < 13; i++)
	{
		v[i].c = mesh->color;
	}
	for(i = 0; i < 5; i++)
	{
		int idx = index_i[i];
		v[2*i].v = v_src[idx];
		v[2*i].t = t_src[idx];
		v[2*i].v.z = height;
	}
	for(j = 1; j < 5; j++)
	{
		v_src = mesh->v + index_j[j];
		t_src = MESH__T + index_j[j];
		
		for(i = 0; i < 5; i++)
		{
			int idx = index_i[i];
			v[2*i+j].v = v_src[idx];
			v[2*i+j].t = t_src[idx];
			v[2*i+j].v.z = height;
		}
		RenderTriStrip(v + index_n[j-1], count_n[j-1]);
	}
	// **********************
	// Render corner polygons
	// **********************
	
	static int corner_index_table[] = {
		16, 2, 14, 1, 7,
		18, 20, 4, 13, 5,
		32, 46, 34, 47, 41,
		30, 28, 44, 35, 43};
	
	for(j = 0; j < 4; j++)
	{
		if(!IsCorner(tile, j)) continue;
		
		int *corner_index = corner_index_table + 5*j;
		
		for(i = 0; i < 5; i++)
		{
			int idx = corner_index[i];
			v[i].v = mesh->v[idx];
			v[i].t = MESH__T[idx];
			v[i].v.z = height;
		}
		RenderTriStrip(v, 5);
	}
}

static void DrawWall(tile_t *tile, int k, float height)
{
	mesh_t *mesh = tile->mesh;
	
	int i;
	glBindTexture(GL_TEXTURE_2D, MAIN__texid[WALL_TEX_ID]);
	
//	if(!IntersectCS(LimR, tc->vin[k], tc->vin[k+4]) &&
//	   !IntersectCS(LimR, tc->v[k], tc->v[(k+1)&3]) )
//	{
//		return;
//	}
	static char outer_index_table[] = {
		0,2,3,4,6,20,27,34,48,46,45,44,42,28,21,14,0};
	static char inner_index_table[] = {
		7,9,10,11,13,5,19,26,33,47,41,39,38,37,35,43,29,22,15,1};
	static char index_adjust_table[] = {
		8,12,40,36,8};
	static float texcoord_table[] = {
		0.f, .2500f, .5000f, .7500f, 1.f,
		0.f, .2857f, .5714f, .8571f, 1.f,
		0.f, .1429f, .4286f, .7143f, 1.f,
		0.f, .1666f, .5000f, .8333f, 1.f};
	
	vertex_t v[10];
	
	char  *outer_index = outer_index_table + k*4;
	char   inner_index[5];
	float *texcoord = texcoord_table;
	
	memcpy(inner_index, inner_index_table + k*5, 5);
	
	if(IsWall(tile, (k-1)&3))
	{
		inner_index[0] = index_adjust_table[k];
		texcoord += 10;
	}
	if(IsWall(tile, (k+1)&3))
	{
		inner_index[4] = index_adjust_table[k+1];
		texcoord += 5;
	}
	for(i = 0; i < 5; i++)
	{
		vertex_t *vrt = v + 2*i;
		vrt->v = mesh->v[outer_index[i]];
		vrt->t = vec2(0.f, texcoord_table[i]*2.f);
		vrt->c = vec3(.5f, .5f, .5f);
		vrt->v.z = height;
	}
	for(i = 0; i < 5; i++)
	{
		vertex_t *vrt = v + 2*i + 1;
		vrt->v = mesh->v[inner_index[i]];
		vrt->t = vec2(.66f, texcoord[i]*2.f);
		vrt->c = mesh->color;
	}
	RenderTriStrip(v, 10);
}

static void DrawCorner(tile_t *tile, int k, float height)
{
	mesh_t *mesh = tile->mesh;
	
	glBindTexture(GL_TEXTURE_2D, MAIN__texid[CORNER_TEX_ID]);
	
	vertex_t v[3];
	
	static int corner_index_table[12] =
		{0,7,1,6,5,13,48,41,47,42,43,35};
	
	int *corner_index = corner_index_table + 3*k;
	
	v[0].v = mesh->v[corner_index[0]];
	v[1].v = mesh->v[corner_index[1]];
	v[2].v = mesh->v[corner_index[2]];
	v[0].t = vec2(0.f, 0.f);
	v[1].t = vec2(.66f, 0.f);
	v[2].t = vec2(.25f, .66f);
	v[0].c = vec3(.5f, .5f, .5f);
	v[1].c = mesh->color;
	v[2].c = mesh->color;
	v[0].v.z = height;
	
	RenderPoly(v, 3);
}

static void DrawObjects(tile_t *tile)
{
	obj_t *obj = tile->obj;
	
	int i;
	
//	glBindTexture(GL_TEXTURE_2D, MAIN__texid[2]);
	glDisable(GL_TEXTURE_2D);
	while(obj)
	{
		vertex_t v[18]; // MAX(obj->verts) = 16
		mat3 m;
		float theta, dt;
		
		if(obj->flags & OBJ_FLAG_GHOST)
		{
			obj = obj->next;
			continue;
		}
		// *************
		// Expand radius
		// *************
		
		if(obj->r < obj->r_max)
		{
			obj->r += .005f;
			
			if(obj->r > obj->r_max)
			{
				obj->r = obj->r_max;
			}
		}
		// ***********
		// Draw object
		// ***********
		
		m     = tile->mesh->m * obj->m_inv;
		theta = 0.f;
		dt    = 6.2831853f / (float)obj->verts;
		
		v[0].v = HProject(vec2(m.x.z, m.y.z) / m.z.z);
		v[0].c =  vec3(1.f, 1.f, 1.f);
		v[0].v.z = .9f;
		
		for(i = 0; i < obj->verts; i++, theta += dt)
		{
			float s = (float)sin(theta);
			float c = (float)cos(theta);
			
			vec2 tmp = m * (vec2(c, s) * obj->r);
			
			s = (1.f + s) * .5f;
			c = (1.f - s);
			
			v[i+1].v = HProject(tmp);
			v[i+1].c = obj->color1*s + obj->color2*c;
		}
		v[i+1] = v[1];
		
		// 3Dfx Glide
		//grCullMode(GR_CULL_NEGATIVE);
		RenderTriFan(v, i+2);
		
		// ***********
		// Next object
		// ***********
		
		obj = obj->next;
	}
	glEnable(GL_TEXTURE_2D);
}

static void VisitTile(tile_t *tile)
{
	mesh_t *mesh = tile->mesh;
	
	int k;
	
	tile->frame_rendered = CurrentFrame;
	
	// ************************
	// Compute outside vertices
	// ************************
	
	// POTENTIAL BUG: Backface culling uses v_outer - since v_outer
	// is not shared between tiles, traversal may fail in rare cases
	
	vec2 v[4];
	
	v[0] = mesh->m*MESH__V[0];
	v[1] = mesh->m*MESH__V[6];
	v[2] = mesh->m*MESH__V[48];
	v[3] = mesh->m*MESH__V[42];
	
	for(k = 0; k < 4; k++)
	{
		// ***************************************
		// Reject if out-of-view, or already drawn
		// ***************************************
		
		tile_t *next = tile->next[k];
		
		if(!next || next->frame_rendered == CurrentFrame) continue;
		if(!IntersectCS(LimR, v[k], v[(k+1)&3])) continue;
		
		// ****************************
		// Reject if backward traversal
		// ****************************
		
#if BACKFACE_REJECT
		
		vec2 n(v[(k+1)&3].y - v[k].y,
		       v[k].x - v[(k+1)&3].x);
		
		float t = n*v[k];
		
		if(t > 0.f || (t == 0.f && tile > next)) continue;
#endif
		// ****************
		// Update next mesh
		// ****************
		
		mesh_t *mesh2 = next->mesh = &MeshList[MeshCount++];
		
		int i = (FindIndex(tile, next) - k + 2)&3;
		
		mesh2->m    = mesh->m*MESH__M[4*i + k];
		mesh2->tile = next;
		
		UpdateMesh(mesh2);
		
		// ***************************
		// Recurse to neighboring tile
		// ***************************
		
		VisitTile(next);
	}
}

static void PlaceObj(obj_t *obj, tile_t *tile)
{
	obj->next = tile->obj;
	obj->tile = tile;
	tile->obj = obj;
}

static void UpdateObj(obj_t *obj, mat3 &m)
{
	if(obj->flags & OBJ_FLAG_MOVING)
	{
		obj->m = mat3::horthogonalized(m);
		
		while(CollideObjNode(obj));
		
		obj->m_inv = obj->m.inverse();
	}
}

static int CollideObjNode(obj_t *obj)
{
	// ********************************
	// Returns 1 if changed - should be
	// called repeatedly until return 0
	// ********************************
	
	static char outer_index_table[] = {0, 6, 48, 42};
	static char inner_index_table[] = {7, 5, 41, 43, 13, 47, 35, 1};
	
	int k;
	
	if(~obj->flags & OBJ_FLAG_GHOST)
	{
		// ******************
		// Collide with walls
		// ******************
		
		for(k = 0; k < 4; k++)
		{
			if(!IsWall(obj->tile, k)) continue;
			
			vec2 a = obj->m*MESH__V[inner_index_table[k]];
			vec2 b = obj->m*MESH__V[inner_index_table[k+4]];
			vec2 n(b.y - a.y, a.x - b.x);
			
			float d = n*a;
			
			if(d*d < (n*n)*(obj->r*obj->r))
			{
				d /= n.len();
				vec2 a2 = a + n*(obj->r + d);
				vec2 b2 = b + n*(obj->r + d);
				obj->m = mat3::htransline(a2, b2, a, b)*obj->m;
				return 1;
			}
		}
		// ********************
		// Collide with corners
		// ********************
		
		for(k = 0; k < 4; k++)
		{
			if(!IsCorner(obj->tile, k)) continue;
			
			vec2 a = obj->m*MESH__V[inner_index_table[k]];
			vec2 b = obj->m*MESH__V[inner_index_table[((k-1)&3)+4]];
			vec2 n(b.y - a.y, a.x - b.x);
			
			float d = n*a;
			
			if(IntersectCS(obj->r, a, b))
			{
				d /= n.len();
				vec2 a2 = a + n*(obj->r + d);
				vec2 b2 = b + n*(obj->r + d);
				obj->m = mat3::htransline(a2, b2, a, b)*obj->m;
				return 1;
			}
			
		}
	}
	// **********************
	// Traverse between tiles
	// **********************
	
	for(k = 0; k < 4; k++)
	{
		if(obj->flags & OBJ_FLAG_GHOST)
		{
			if(IsWall(obj->tile, k)) continue;
		}
		vec2 a = obj->m*MESH__V[outer_index_table[k]];
		vec2 b = obj->m*MESH__V[outer_index_table[(k+1)&3]];
		
		if(a.x*b.y > a.y*b.x)
		{
			tile_t *next = obj->tile->next[k];
			
			if(!next) continue;
			
			// ******************
			// Update linked list
			// ******************
			
			if(obj->tile->obj == obj)
			{
				obj->tile->obj = obj->next;
			}
			else
			{
				obj_t *o2;
				
				for(o2 = obj->tile->obj; o2; o2 = o2->next)
				{
					if(o2->next == obj) break;
				}
				o2->next = obj->next;
			}
			obj->next = next->obj;
			next->obj = obj;
			
			// ****************
			// Update transform
			// ****************
			
			int i = (FindIndex(obj->tile, next) - k + 2)&3;
			
			obj->m = obj->m*MESH__M[4*i + k];
			obj->tile = next;
			
			return 1;
		}
	}
	return 0;
}

/* 3Dfx Glide
static int GetSwapSync(void)
{
	static long *swap_history = NULL;
	static long num_entires;
	
#if RENDER
	if(!swap_history)
	{
		grGet(GR_NUM_SWAP_HISTORY_BUFFER, 4, &num_entires);
		swap_history = (long*)malloc(sizeof(long) * num_entires);
		// test swap_history != NULL
	}
	grGet(GR_SWAP_HISTORY, 4*num_entires, swap_history);
	
	return swap_history[0] + 1;
#else
	return 1;
#endif
}
*/

static int cmpfn(const void *a, const void *b)
{
	mesh_t *mesh_a = *((mesh_t**)a);
	mesh_t *mesh_b = *((mesh_t**)b);
	
	float dist_a = mesh_a->v[24]*mesh_a->v[24];
	float dist_b = mesh_b->v[24]*mesh_b->v[24];
	
	if(dist_a < dist_b) return +1; else
	if(dist_a > dist_b) return -1; else
	return 0;
}

static void DrawScene(obj_t *obj)
{
	mesh_t *mesh;
	mesh_t *tmp_meshlist[MAX_ONSCREEN_TILES];
	
	int i, k;
	
	mesh       = obj->tile->mesh = &MeshList[0];
	mesh->m    = obj->m;
	mesh->tile = obj->tile;
	
	UpdateMesh(mesh);
	MeshCount = 1;
	
	VisitTile(obj->tile); // recursively visit tiles
	
	for(i = 0; i < MeshCount; i++)
	{
		tmp_meshlist[i] = &MeshList[i];
	}
	qsort(tmp_meshlist, MeshCount, sizeof(mesh_t*), cmpfn);
	
	// *******
	// Layer 1
	// *******
	
	for(i = 0; i < MeshCount; i++)
	{
		tile_t *tile = tmp_meshlist[i]->tile;
		
		if(IsFloor(tile))
		{
			DrawFloor(tile, 1.f);
		}
	}
	// *******
	// Layer 2
	// *******
	
	for(i = 0; i < MeshCount; i++)
	{
		tile_t *tile = tmp_meshlist[i]->tile;
				
		if(IsFloor(tile))
		{
			for(k = 0; k < 4; k++)
			{
				if(IsWall(tile, k))
				{
					DrawWall(tile, k, .85f);
				}
				if(IsCorner(tile, k))
				{
					DrawCorner(tile, k, .85f);
				}
			}
			DrawObjects(tile);
		}
	}
	// *******
	// Layer 3
	// *******
	
	for(i = 0; i < MeshCount; i++)
	{
		tile_t *tile = tmp_meshlist[i]->tile;
		
		if(!IsFloor(tile))
		{
			DrawFloor(tile, .85f);
		}
	}
}

// **************************************************************************************
// **************************************************************************************
// *** Private Initialization Functions
// **************************************************************************************
// **************************************************************************************

static void SubdivideEdge(vec2 v[], int skip)
{
	v[skip*3] = vec2::hmidpoint(v[0], v[skip*6]);
	v[skip*2] = vec2::hmidpoint(v[0], v[skip*3]);
	v[skip*1] = vec2::hmidpoint(v[0], v[skip*2]);
	v[skip*4] = vec2::hmidpoint(v[skip*6], v[skip*3]);
	v[skip*5] = vec2::hmidpoint(v[skip*6], v[skip*4]);
}

static void CalcMTV(mat3 m[16], vec2 t[49], vec2 v[49])
{
	int i, j;
	
	// *****************************************
	// Compute matrices used in tiling traversal
	// *****************************************
	
	float x1 = TileLength*2.f;
	float x2 = TileLength*TileLength;
	
	m[0] = mat3(1.f+x2, 0.f, -x1, 0.f, 1.f-x2, 0.f, -x1, 0.f, 1.f+x2);
	m[1] = mat3(1.f-x2, 0.f, 0.f, 0.f, 1.f+x2, +x1, 0.f, +x1, 1.f+x2);
	m[2] = mat3(1.f+x2, 0.f, +x1, 0.f, 1.f-x2, 0.f, +x1, 0.f, 1.f+x2);
	m[3] = mat3(1.f-x2, 0.f, 0.f, 0.f, 1.f+x2, -x1, 0.f, -x1, 1.f+x2);
	
	for(j = 0; j < 12; j += 4)
	{
		for(i = 0; i < 4; i++)
		{
			m[i+j+4] = mat3(m[i+j].x.y, -m[i+j].x.x, m[i+j].x.z,
			                m[i+j].y.y, -m[i+j].y.x, m[i+j].y.z,
			                m[i+j].z.y, -m[i+j].z.x, m[i+j].z.z);
		}
	}
	// **********************
	// Compute texture coords
	// **********************
	
	static float tcomp[7] = {0.f, .125f, .25f, .5f, .75f, .875f, 1.f};
	
	for(j = 0; j < 7; j++)
	{
		for(i = 0; i < 7; i++)
		{
			t[7*j+i] = vec2(tcomp[i], tcomp[j]);
		}
	}
	// ***********************************
	// Compute untransformed tile vertices
	// ***********************************
	// 
	//	 6--13--20------27------34--41--48
	//	 |   |   |       |       |   |   |
	//	 5--12--19------26------33--40--47
	//	 |   |   |       |       |   |   |
	//	 4--11--18------25------32--39--46
	//	 |   |   |       |       |   |   |
	//	 |   |   |       |       |   |   |
	//	 |   |   |       |       |   |   |
	//	 |   |   |       |       |   |   |
	//	 3--10--17------24------31--38--45
	//	 |   |   |       |       |   |   |
	//	 |   |   |       |       |   |   |
	//	 |   |   |       |       |   |   |
	//	 |   |   |       |       |   |   |
	//	 2---9--16------23------30--37--44
	//	 |   |   |       |       |   |   |
	//	 1---8--15------22------29--36--43
	//	 |   |   |       |       |   |   |
	//	 0---7--14------21------28--35--42
	
	v[0]  = vec2(-TileLength, -TileLength);
	v[6]  = vec2(-TileLength, +TileLength);
	v[42] = vec2(+TileLength, -TileLength);
	v[48] = vec2(+TileLength, +TileLength);
	
	SubdivideEdge(v+0,  7);
	SubdivideEdge(v+6,  7);
	SubdivideEdge(v+0,  1);
	SubdivideEdge(v+42, 1);
	
	for(i = 7; i < 42; i += 7)
	{
		SubdivideEdge(v + i, 1);
	}
}

// **************************************************************************************
// **************************************************************************************
// *** Public Functions
// **************************************************************************************
// **************************************************************************************

static int IsNeighborFree(tile_t *tile)
{
	int k;

	for(k = 0; k < 4; k++)
	{
		tile_t *next = tile->next[k];
		
		if(next && IsFree(next)) return 1;
	}
	return 0;
}

void HyperTile_Init(int size)
{
	int i, k;
	
	CalcMTV(MESH__M, MESH__T, MESH__V);
	
	// ***************
	// Generate tiling
	// ***************
	
	int total;
	
	AddTile(NULL);
	
	for(i = total = 1; i < size+4; i++)
	{
		int j, count = TileCount;
		
		for(j = 0; j < count; j++)
		{
			tile_t *tile = &TileList[j];
			
			for(k = 0; k < 4; k++)
			{
				if(!tile->next[k])
				{
					tile_t *next  = AddTile(tile);
					tile->next[k] = next;
					CurlLeft(next);
					CurlRight(next);
				//	total++;
					
					if(i >= size) ClearFree(next);
					else total++;
				}
			}
		}
	}
	// *************
	// Generate maze
	// *************
	
	int count;
	int max = total / 2;
	
	tile_t *tile, *next, *path[1000];
	int pathlen = 0;
	int pathcount;
	
	tile = &TileList[0];
	ClearFree(tile);
	
	for(count = 1; count < max;)
	{
		// ************************************************
		// Find a visited tile with a non-visited neighbor:
		// First, try to branch off previous path
		// ************************************************
		
		// **************************************
		// If branch fails, find some other point
		// **************************************
		
		do
		{
			static int j = 0;
			
			do
			{
				j++; if(j == total) j = 0;
				tile = &TileList[j];
			}
			while(IsFree(tile));
		}
		while(!IsNeighborFree(tile));
		
		// **************
		// Construct path
		// **************
		
		pathlen   = 0;
		pathcount = rand() % 128 + 1;
		
		for(; count < max && pathlen < pathcount; count++)
		{
			if(rand() % 20 == 0) i++; // change direction
			
			for(k = 0; k < 4; k++)
			{
				next = tile->next[(i+k)&3];
				
				if(next && IsFree(next)) break;
			}
			if(k == 4) break; // end of path
			
			i = (i+k)&3;
			path[pathlen++] = next;
			ClearWall(tile, i);
			i = FindIndex(tile, next);
			ClearWall(next, i);
			ClearFree(next);
			tile = next;
			i = (i+2)&3;
		}
	}
	// ******************************
	// Remove "dangling" border tiles
	// ******************************
	
	int removed = 0;
	
	if(1) for(i = 0; i < TileCount; i++)
	{
		tile = &TileList[i];
		
		int b = 0;
		
		for(k = 0; k < 4; k++)
		{
			if(!tile->next[k]) b++;
			else next = tile->next[k];
		}
		if(b == 3)
		{
			SetFree(tile);
			
			b = FindIndex(tile, next);
			
			next->next[b] = NULL;
			SetWall(next, b);
			
			removed++;
		}
	}
	// ***********
	// Count walls
	// ***********
	
	int wallcount = 0;
	
	for(i = 0; i < TileCount; i++)
	{
		tile = &TileList[i];
		
		if(IsFree(tile)) continue;
		
		for(k = 0; k < 4; k++)
		{
			if(tile->next[k] && IsWall(tile, k))
			{
				wallcount++;
			}
		}
	}
	wallcount /= 2;
	
	// ****************************************
	// Remove some more walls to make it harder
	// ****************************************
	
	if(1) for(i = 0; i < 1; i++)
	{
		do
		{
			do
			{
				tile = &TileList[rand() % TileCount];
			}
			while(IsFree(tile));
			
			for(k = 0; k < 4; k++)
			{
				next = tile->next[k];
				
				if(next && IsWall(tile, k)) break;
			}
		}
		while(k == 4);
		
		ClearWall(tile, k);
		ClearWall(next, FindIndex(tile, next));
	}
	// ****************
	// Set corner flags
	// ****************
	
	for(i = 0; i < TileCount; i++)
	{
		tile = &TileList[i];
		
		if(IsFree(tile)) continue;
		
		for(k = 0; k < 4; k++)
		{
			if(!IsWall(tile, k) && !IsWall(tile, (k-1)&3))
			{
				tile_t *tile2 = tile;
				int k2 = k;
				
				do
				{
					if(IsWall(tile2, k2))
					{
						SetCorner(tile, k);
						break;
					}
					next = tile2->next[k2];
					k2 = (FindIndex(tile2, next) + 1)&3;
					tile2 = next;
				}
				while(tile2 != tile);
			}
		}
	}
	// ************************
	// Set non-floor tile flags
	// ************************
	
	removed = 0;
	
	if(1) for(i = 0; i < TileCount; i++)
	{
		tile = &TileList[i];
		
		if(IsWall(tile, 0) && IsWall(tile, 1) &&
		   IsWall(tile, 2) && IsWall(tile, 3) )
		{
			ClearWall(tile, 0);
			ClearWall(tile, 1);
			ClearWall(tile, 2);
			ClearWall(tile, 3);
			ClearFloor(tile);
			removed++;
		}
	}
	TileCount -= removed;
}

/*
long HyperTile_InitHW(long window)
{
	// *****************
	// Create a fog mask
	// *****************
	
	short *fogmask;
	int x, y;
	int area = SCREEN_RES_X*SCREEN_RES_Y;
	
	fogmask = (short*)malloc(sizeof(short)*area);
	
	for(y = 0; y < SCREEN_RES_Y; y++)
	{
		for(x = 0; x < SCREEN_RES_X; x++)
		{
			vec2 v = vec2((float)(x-SCREEN_RES_X/2),
			              (float)(y-SCREEN_RES_Y/2));
			
			v /= (float)(SCREEN_RES_Y/2);
			
			float fog  = 1.f - (float)pow(v*v, 3.f);
			short fogi = (short)(fog*255.f);
			
			if(fogi <   0) fogi =   0;
			if(fogi > 255) fogi = 255;
			
		//	fogi = 255; // no fog
			
			if(v*v > 1.f) fogi = 0;
			
			fogmask[x + y*SCREEN_RES_X] = fogi;
		}
	}
	// **************************************
	// Initialize 3Dfx Glide rendering system
	// **************************************
	
	long context = 1;
	
#if RENDER
	
	grGlideInit();
	grSstSelect(0);
	context = (long)grSstWinOpen(
		window,
		GR_RESOLUTION_800x600,
		GR_REFRESH_60Hz,
		GR_COLORFORMAT_RGBA,
		GR_ORIGIN_LOWER_LEFT, 2, 1);
	if(!context) return 0;
	
	// *************************************
	// Configure Glide to receive alpha mask
	// *************************************
	
	grCoordinateSpace(GR_WINDOW_COORDS);
	grViewport(0, 0, SCREEN_RES_X, SCREEN_RES_Y);
	grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
	grVertexLayout(GR_PARAM_W, 8, GR_PARAM_ENABLE);
	grVertexLayout(GR_PARAM_A, 12, GR_PARAM_ENABLE);
	grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
	grDepthMask(FXFALSE);
	grColorMask(FXTRUE, FXTRUE);
	grAlphaCombine(
		GR_COMBINE_FUNCTION_LOCAL,
		GR_COMBINE_FACTOR_ONE,
		GR_COMBINE_LOCAL_ITERATED,
		GR_COMBINE_OTHER_NONE,
		FXFALSE);
	grAlphaBlendFunction(
		GR_BLEND_ZERO,
		GR_BLEND_ZERO,
		GR_BLEND_ONE,
		GR_BLEND_ZERO);
	
	// *****************************
	// Copy fog mask to alpha buffer
	// *****************************
	
	for(y = 0; y < SCREEN_RES_Y; y++)
	{
		for(x = 0; x < SCREEN_RES_X; x++)
		{
			struct vtmp_t
			{
				float x, y, z, a;
			};
			vtmp_t v;
			
			v.x = (float)x;
			v.y = (float)y;
			v.z = 1.f;
			v.a = (float)fogmask[x + y*SCREEN_RES_X];
			
			RenderPoint(&v);
		}
	}
	// *****************************
	// Configure Glide for rendering
	// *****************************
	
	grCoordinateSpace(GR_CLIP_COORDS);
	grVertexLayout(GR_PARAM_A, -1, GR_PARAM_DISABLE);
	grVertexLayout(GR_PARAM_RGB, 12, GR_PARAM_ENABLE);
	grVertexLayout(GR_PARAM_ST0, 24, GR_PARAM_ENABLE);
	grViewport(
		(SCREEN_RES_X-SCREEN_RES_Y)/2, 0,
		 SCREEN_RES_Y,SCREEN_RES_Y);
	grTexClampMode(
		GR_TMU0,
		GR_TEXTURECLAMP_WRAP,
		GR_TEXTURECLAMP_WRAP);
	grTexFilterMode(
		GR_TMU0,
		GR_TEXTUREFILTER_BILINEAR,
		GR_TEXTUREFILTER_BILINEAR);
	grTexMipMapMode(
		GR_TMU0,
		GR_MIPMAP_NEAREST_DITHER,
		FXFALSE);
	grEnable(GR_ALLOW_MIPMAP_DITHER); // slow, but we'll use it for now
	grTexCombine(
		GR_TMU0,
		GR_COMBINE_FUNCTION_LOCAL,
		GR_COMBINE_FACTOR_NONE,
		GR_COMBINE_FUNCTION_LOCAL,
		GR_COMBINE_FACTOR_NONE,
		FXFALSE,
		FXFALSE);
	grColorCombine(
		GR_COMBINE_FUNCTION_LOCAL,
		GR_COMBINE_FACTOR_LOCAL,
		GR_COMBINE_LOCAL_ITERATED,
		GR_COMBINE_OTHER_NONE,
		FXFALSE);
	grAlphaBlendFunction(
		GR_BLEND_DST_ALPHA,
		GR_BLEND_ZERO,
		GR_BLEND_ZERO,
		GR_BLEND_ZERO);
	grColorMask(FXTRUE, FXFALSE);
	Render_LoadTexture("STONE01.3df");
#endif
	
	return (long)context;
}
*/

void HyperTile_Draw(mat3 &m, float scale, float morph, int drop)
{
	static obj_t player;
	static obj_t ghost;
	
	static int inited = 1;
	
	// *******************
	// Place player object
	// *******************
	
	if(inited)
	{
		inited = 0;
		
		player.flags  = OBJ_FLAG_PLAYER | OBJ_FLAG_MOVING | OBJ_FLAG_CAMERA;
		player.m.identity();
		player.m_inv  = player.m;
		player.r      = .2f;
		player.r_max  = .2f;
		player.next   = NULL;
		player.color1 = vec3(0.f, 1.f, 0.f);
		player.color2 = vec3(0.f, 0.f, 1.f);
		player.verts  = 16;
		
		ghost = player;
		ghost.flags = OBJ_FLAG_GHOST | OBJ_FLAG_MOVING | OBJ_FLAG_CAMERA;
		
		PlaceObj(&player, TileList);
		PlaceObj(&ghost,  TileList);
	}
	// ***************************
	// Place dropped object marker
	// ***************************
	
	if(drop && ObjectCount < MAX_WORLD_OBJECTS)
	{
		obj_t *obj = &ObjectList[ObjectCount++];
		
		obj->flags  = 0;
		obj->m      = player.m;
		obj->m_inv  = player.m_inv;
		obj->r      = 0.f;
		obj->r_max  = vec3::random().x * .1f + .1f;
		obj->next   = NULL;
		obj->color1 = vec3::random().normalized() * .5f;
		obj->color2 = vec3::random().normalized();
		obj->verts  = (rand() % 5) + 3;
		
		PlaceObj(obj, player.tile);
	}
	// *********************************
	// Update scaling and visible radius
	// *********************************
	
	float q = scale*scale + morph*morph;
	
	ProjectionScale = scale;
	ProjectionMorph = morph;
	LimR = (scale + morph*(float)sqrt(q - 1.f)) / q;
	
	// ***********************************
	// Update player transform and physics
	// ***********************************
	
	// NOTE: Updating player object initializes the first
	// tile for rendering. No subsequent objects should be
	// updated between updating the player and drawing
	// the tiling.
	
	if(player.tile->frame_visited < 1)
	{
		player.tile->frame_visited = CurrentFrame;
		TILES_HIT++;
	}
	// *******************
	// Draw "outside area"
	// *******************
	if(0)
	{
		vertex_t v[4];
		
		v[0].v = vec2(-1.f, -1.f);
		v[1].v = vec2(+1.f, -1.f);
		v[2].v = vec2(+1.f, +1.f);
		v[3].v = vec2(-1.f, +1.f);
		v[0].c = v[1].c = v[2].c = v[3].c = vec3(.7f, 1.f, .2f);
#if RENDER
		/* 3Dfx Glide
		grColorCombine(
			GR_COMBINE_FUNCTION_LOCAL,
			GR_COMBINE_FACTOR_NONE,
			GR_COMBINE_LOCAL_ITERATED,
			GR_COMBINE_OTHER_NONE,
			FXFALSE);
			*/
#endif
		RenderPoly(v, 4);
	}
	// **********************
	// Draw tiles and objects
	// **********************
	
	CurrentFrame++;
	
#if RENDER
	/* 3Dfx Glide
	grColorCombine(
		GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL,
		GR_COMBINE_FACTOR_ONE,
		GR_COMBINE_LOCAL_ITERATED,
		GR_COMBINE_OTHER_TEXTURE,
		FXFALSE);
		*/
	float tec[] = {1.f, 1.f, 1.f, 1.f};
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
	glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, tec);
#endif
	DrawScene(&ghost);
	
	// 3Dfx Glide
	//int sync = GetSwapSync();
	int sync = 1;
	
	while(sync--)
	{
		UpdateObj(&player, m*player.m);	
#if 1
		if(player.tile == ghost.tile)
	//	if(1)
		{
		mat3 target = ghost.tile->mesh->m * player.tile->mesh->m.inverse() * player.m;
		target /= target.z.z;
		ghost.m /= ghost.m.z.z;
		UpdateObj(&ghost, ghost.m*.95f + target*.05f);
		}
		else
		{
		vec2 target = player.m_inv * vec2(0.f, 0.f);
		target = player.tile->mesh->m * target;
		target *= -.05f;
		UpdateObj(&ghost, mat3::htranslate(target)*ghost.m);
		
	//	float theta1, theta2;
	//	vec2  trans1, trans2;
	//	mat3::hdecompose(theta1, trans1, ghost.m);
	//	mat3::hdecompose(theta2, trans2, player.m);
	//	theta1 = .98f*theta1 + .02f*theta2;
	//	trans1 = trans1*.98f + trans2*.02f;
	//	UpdateObj(&ghost, mat3::htranslate(trans1)*mat3::rotZ(theta1));
		}
#else
		ghost.m = player.m;
		ghost.tile = player.tile;
#endif
	}
	// Draw FOG MASK (OpenGL)
	if(1)
	{
		glDisable(GL_DEPTH_TEST);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor3f(1.f, 1.f, 1.f);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ZERO, GL_SRC_COLOR);
		glBindTexture(GL_TEXTURE_2D, MAIN__texid[0]);
		glBegin(GL_POLYGON);
		glTexCoord2f(-.15f, -.15f); glVertex3f(-1.f, -1.f, 0.4f);
		glTexCoord2f(1.15f, -.15f); glVertex3f( 1.f, -1.f, 0.4f);
		glTexCoord2f(1.15f, 1.15f); glVertex3f( 1.f,  1.f, 0.4f);
		glTexCoord2f(-.15f, 1.15f); glVertex3f(-1.f,  1.f, 0.4f);
		glEnd();
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
	}
	// *****************
	// Draw progress bar
	// *****************
	if(1)
	{
		vertex_t v[4];
		
		v[0].v = vec3(-1.f, -.94f,1.1f);
		v[1].v = vec3(+1.f, -.94f,1.1f);
		v[2].v = vec3(+1.f, -1.f, 1.1f);
		v[3].v = vec3(-1.f, -1.f, 1.1f);
		v[0].c = v[1].c = v[2].c = v[3].c = vec3(0.f, 0.f, .5f);
		
#if RENDER
		/* 3Dfx Glide
		grAlphaBlendFunction(
			GR_BLEND_ONE,
			GR_BLEND_ZERO,
			GR_BLEND_ZERO,
			GR_BLEND_ZERO);
		grColorCombine(
			GR_COMBINE_FUNCTION_LOCAL,
			GR_COMBINE_FACTOR_NONE,
			GR_COMBINE_LOCAL_ITERATED,
			GR_COMBINE_OTHER_NONE,
			FXFALSE);
			*/
#endif
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_TEXTURE_2D);
		RenderPoly(v, 4);
		if(TileCount-2 == TILES_HIT)
		{
			v[0].c = v[1].c = v[2].c = v[3].c = vec3::random();
		}
		else
		{
			v[0].c = v[1].c = v[2].c = v[3].c = vec3(0.f, 0.f, 1.f);
		}
		v[1].v.x = v[2].v.x = -1.f + 2.f*(float)TILES_HIT/(float)(TileCount-2);
		RenderPoly(v, 4);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_TEXTURE_2D);
#if RENDER
		/* 3Dfx Glide
		grAlphaBlendFunction(
			GR_BLEND_DST_ALPHA,
			GR_BLEND_ZERO,
			GR_BLEND_ZERO,
			GR_BLEND_ZERO);
			*/
#endif
	}
#if RENDER
	/* 3Dfx Glide
	grBufferSwap(1);
	*/
#endif
}

// **************************************************************************************
// **************************************************************************************
// *** Main Program and GLUT Stuff
// **************************************************************************************
// **************************************************************************************

static void GLUT__Display(void)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	MAIN__RenderScene();
	MAIN__TrackFPS();
	{
		static vec2 vel(0.f, 0.f);
		static float scale = 1.4f; // 1.4 = 33tiles, 1.2 = 54tiles, 1.1 = 105tiles
		static float morph = 1.0f;
		
		static float maxvl = .0650f;
		static float accel = .0030f;
		
		static int drop_counter = 0;
		int drop = 0;
		
		if(GetAsyncKeyState(VK_SPACE) & 0x8000)
		{
			if(drop_counter > 15)
			{
				drop = 1;
				drop_counter = 0;
			}
		}
		drop_counter++;
		
		float m_old = morph;
		
		vel *= .9f;
		if(GetAsyncKeyState(VK_NUMPAD4) & 0x8000) vel.x += accel;
		if(GetAsyncKeyState(VK_NUMPAD6) & 0x8000) vel.x -= accel;
		if(GetAsyncKeyState(VK_NUMPAD2) & 0x8000) vel.y += accel;
		if(GetAsyncKeyState(VK_NUMPAD8) & 0x8000) vel.y -= accel;
		if(GetAsyncKeyState(VK_UP)      & 0x8000) scale += .01f;
		if(GetAsyncKeyState(VK_DOWN)    & 0x8000) scale -= .01f;
		if(GetAsyncKeyState(VK_LEFT)    & 0x8000) morph += .01f;
		if(GetAsyncKeyState(VK_RIGHT)   & 0x8000) morph -= .01f;
		
		if(morph < 0.00f) morph = 0.00f;
		if(morph > 1.00f) morph = 1.00f;
		
		if(morph != m_old)
		{
			scale *= 1.f + morph*(float)sqrt(.75f);
			scale /= 1.f + m_old*(float)sqrt(.75f);
		}
		if(scale < 1.02f) scale = 1.02f;
		if(scale > 2.00f) scale = 2.00f;
		
		if(vel.len() > maxvl) vel *= maxvl / vel.len();
		
		HyperTile_Draw(mat3::htranslate(vel), scale, morph, drop);
	}
	glFlush();
    glutSwapBuffers();
}

static void GLUT__Reshape(int width, int height)
{
	int offset_x = 0;
	int offset_y = 0;
	
	if(width > height)
	{
		offset_x = (width - height)/2;
		width = height;
	}
	else
	{
		offset_y = (height - width)/2;
		height = width;
	}
//	float aspect = (float)height / (float)width;
	float aspect = 1.0f;
	
	glViewport(offset_x, offset_y, width, height);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glScalef(1.f, 1.f, -1.f);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(60.f, 1.f/aspect, .001f, 100.f);
	glTranslatef(0.f, 0.f, -1.f);
}

static void GLUT__Keyboard(unsigned char key, int x, int y)
{
	if(key == 27) MAIN__STOP = 1; // ESC
}

static void GLUT__Mouse(int button, int state, int x, int y)
{
}

static void GLUT__Idle(void)
{
	if(WAIT_FOR_CLOCK) Sleep(8);
	glutPostRedisplay();
}

void main(int argc, char *argv[])
{
	fprintf(stdout, "HypEngine: Hyperbolic Maze Demo\n");
	fprintf(stdout, "(c) Bernie Freidin, 1999-2000\n\n");
	fprintf(stdout, "Keys:\n");
	fprintf(stdout, "  Numeric keypad to move (NumLock ON)\n");
	fprintf(stdout, "  Arrow keys to zoom in/out and adjust view\n");
	fprintf(stdout, "  SPACE to drop an object\n");
	fprintf(stdout, "  ESCAPE to quit\n\n");
	fprintf(stdout, "Tips:\n");
	fprintf(stdout, "  Zoom in and adjust view RIGHT for faster framerate\n");
	fprintf(stdout, "  When maze is fully explored, status bar flashes\n");
	
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
	glutInitWindowSize(640, 480);
	glutInitWindowPosition(40, 40);
	glutCreateWindow(argv[0]);
	glutDisplayFunc(GLUT__Display);
	glutReshapeFunc(GLUT__Reshape);
	glutKeyboardFunc(GLUT__Keyboard);
	glutMouseFunc(GLUT__Mouse);
	glutIdleFunc(GLUT__Idle);
	
	const unsigned char *ext = glGetString(GL_EXTENSIONS);
	
	glClearColor(0.f, 0.f, 0.f, 1.0f);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
	
	glGenTextures(MAIN__texcount, MAIN__texid);
	
	for(int i = 0; i < MAIN__texcount; i++)
	{
		glBindTexture(GL_TEXTURE_2D, MAIN__texid[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		
		if(i > 0)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			MAIN__LoadTGA(MAIN__texnames[i-1]);
		}
		else
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			MAIN__LoadAlphaMask(512);
		}
	}
	HyperTile_Init(7); // 7
	// 3Dfx Glide
	//context = HyperTile_InitHW((long)hwnd);
	//if(!context) return;
	
	glutMainLoop();
}

/*
COMPUTING "TileLength"...

hdist[v1_,v2_] = (1-v1.v2)/Sqrt[(1-v1.v1)(1-v2.v2)];
p0 =  {0,0};
p1 = a{1,0};
p2 = a{Cos[2Pi/5],Sin[2Pi/5]};
p3 = b{Cos[1Pi/5],Sin[1Pi/5]};
Solve[{
hdist[p0,p3] - hdist[p1,p2] == 0,
hdist[p0,p1] - hdist[p1,p3] == 0}, {a, b}]
:a is the Euclidean length along an edge
:b is the Euclidean length along a diagonal
(assuming the square's corner is resting at 0,0)
*/