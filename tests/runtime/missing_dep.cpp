#if defined(_WIN32)
extern "C" __declspec(dllexport) int lmms_missing_dep_anchor()
{
	return 1;
}
#else
extern "C" int lmms_missing_dep_anchor()
{
	return 1;
}
#endif
