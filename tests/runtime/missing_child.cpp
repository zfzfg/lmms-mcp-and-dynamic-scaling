#if defined(_WIN32)
extern "C" __declspec(dllimport) int lmms_missing_dep_anchor();
int main()
{
	return lmms_missing_dep_anchor() == 1 ? 0 : 2;
}
#else
int main()
{
	return 0;
}
#endif
