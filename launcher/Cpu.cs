using System.Runtime.Intrinsics.X86;

namespace EAMMALauncher;

/// <summary>Picks the game exe for this CPU. A release may ship two builds of the same game (as UFC's does; EA MMA has only eamma.exe so far):
/// eamma.exe = the x86-64-v3 build (AVX2/BMI/MOVBE, ~5% faster) and eamma_compat.exe = the plain x86-64 build. The v3 exe
/// crashes at once on CPUs without those instructions (Intel before Haswell, many Pentium/Celeron, AMD before
/// Excavator), so the launcher starts eamma_compat.exe there. Dev and test folders have only eamma.exe: then that runs.</summary>
public static class Cpu
{
    /// <summary>All x86-64-v3 features: AVX, AVX2, BMI1, BMI2, FMA, LZCNT, MOVBE, F16C (plus OS support for AVX state,
    /// which Avx.IsSupported already checks).</summary>
    public static bool SupportsX64V3
    {
        get
        {
            try
            {
                // Test override: behave like a CPU without x86-64-v3 (checks the eamma_compat.exe path on any PC).
                if (Environment.GetEnvironmentVariable("EAMMA_FORCE_COMPAT") == "1") return false;
                if (!(Avx.IsSupported && Avx2.IsSupported && Bmi1.IsSupported && Bmi2.IsSupported && Fma.IsSupported &&
                      Lzcnt.IsSupported && X86Base.IsSupported))
                    return false;
                var (_, _, ecx, _) = X86Base.CpuId(1, 0);
                const int Movbe = 1 << 22, F16c = 1 << 29;
                return (ecx & Movbe) != 0 && (ecx & F16c) != 0;
            }
            catch { return false; }
        }
    }

    /// <summary>The exe to start: eamma_compat.exe when it exists and this CPU lacks x86-64-v3, else eamma.exe.</summary>
    public static string GameExeForThisCpu()
    {
        if (File.Exists(Paths.CompatExe) && !SupportsX64V3) return Paths.CompatExe;
        return Paths.GameExe;
    }

    /// <summary>One line for the launcher logs.</summary>
    public static string Describe() =>
        $"cpu x86-64-v3 {(SupportsX64V3 ? "yes" : "no")}, compat exe {(File.Exists(Paths.CompatExe) ? "present" : "absent")} -> {Path.GetFileName(GameExeForThisCpu())}";
}
