/* Optional presentation API. The existing renderer API 3 structures are unchanged. */
#ifndef REF_PRESENT_H
#define REF_PRESENT_H

#define REF_PRESENT_VERSION 1
#define REF_PRESENT_VERSION2 2
#define REF_PRESENT_VERSION3 3
/* Selector 4 belongs to the optional display API in ref_display.h. */
#define REF_PRESENT_VERSION5 5
#define PRESENT_BASE 0
#define PRESENT_HUD 1
#define PRESENT_CONSOLE 2

typedef struct
{
	int version, size;
	float (EXPORT *GetSize) (float scale, int *width, int *height);
	void (EXPORT *SetSpace) (int space, float scale, const char *font);
	void (EXPORT *DrawColorPic) (int x, int y, int w, int h,
		const char *pic, const float *rgba);
} refpresent_t;

/* Version 2 is a separate table; version 1 retains its exact layout.
 * Begin/End form one physical-pixel scope. Nested scopes are unsupported.
 * Clip with a nonpositive width or height disables clipping.
 */
typedef struct
{
	int version, size;
	void (EXPORT *Begin) (void);
	void (EXPORT *End) (void);
	void (EXPORT *Glyph) (float x, float y, float w, float h, int c,
		const char *font, const float *rgba);
	void (EXPORT *Pic) (float x, float y, float w, float h,
		const char *pic, const float *rgba);
	void (EXPORT *Fill) (float x, float y, float w, float h, const float *rgba);
	void (EXPORT *Clip) (int x, int y, int w, int h);
	unsigned int (EXPORT *Color8) (int index);
} refpresent2_t;

/* Version 3 carries optional beam colors alongside an unchanged entity array.
 * The renderer copies the metadata. Passing NULL clears the current scene. */
typedef struct
{
	int active;
	unsigned int rgba;
} refbeam_t;

typedef struct
{
	int version, size;
	void (EXPORT *SetBeams) (const void *entities, const refbeam_t *beams, int count);
} refpresent3_t;

extern const refpresent3_t *scr_beam_presentation;
void SCR_SetBeamPresentation (const refpresent3_t *api);

/* Version 5 presents opaque, top-down RGBA8 frames. Stride is positive and
 * includes row padding; data_size covers every pixel through the last row.
 * Update borrows input storage for the call; alpha is ignored. Draw reuses the
 * uploaded frame without transferring pixels again. Both require the normal
 * 2D pass on texture unit zero. Coordinates use the current presentation space.
 * Return zero on invalid input, upload failure or unavailable cached frame.
 * Failed/legacy raw updates and renderer shutdown invalidate the cached frame;
 * a decoder must upload its current frame again after renderer restart.
 * Existing presentation tables are unchanged.
 */
typedef struct
{
	int version, size;
	int (EXPORT *UpdateColorFrame) (int cols, int rows, int stride,
		const unsigned char *rgba, unsigned int data_size);
	int (EXPORT *DrawColorFrame) (int x, int y, int w, int h);
} refpresent5_t;

extern const refpresent5_t *scr_color_presentation;
void SCR_SetColorPresentation (const refpresent5_t *api);

typedef const refpresent_t *(EXPORT *GetPresentationAPI_t) (int version);

/* Client composition state, kept outside all public client/game structures. */
typedef struct
{
	int width, height, x, y, vwidth, vheight;
	float factor;
} present_scope_t;

extern const refpresent_t *scr_presentation;
extern const refpresent2_t *scr_rich_presentation;
void SCR_SetRichPresentation (const refpresent2_t *api);
void SCR_SetPresentation (const refpresent_t *api);
void SCR_BeginPresentation (present_scope_t *saved, int space);
void SCR_EndPresentation (const present_scope_t *saved);
int SCR_ConsoleWidth (void);

#endif
