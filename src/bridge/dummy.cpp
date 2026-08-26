// Dummy compilation unit so NMake can build the target.
// stgr_bridge is a header-only library; this file is never compiled on
// VS generators (which handle object-less libs) but NMake requires at
// least one object file.