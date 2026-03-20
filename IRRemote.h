#ifndef IR_REMOTE_H
#define IR_REMOTE_H

// Initialize IR receiver subsystem.
void irRemoteBegin();

// Poll IR receiver; call from loop().
void irRemoteLoop();

#endif
