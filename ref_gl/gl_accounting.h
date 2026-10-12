#ifndef R1Q2_GL_ACCOUNTING_H
#define R1Q2_GL_ACCOUNTING_H

/* Opt-in renderer API-call accounting. These functions never query the driver. */
void R_AccountBeginFrame (void);
void R_AccountEndFrame (void);
void R_AccountShutdown (void);

#endif
