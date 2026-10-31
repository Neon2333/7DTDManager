using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;

if (args.Length < 1)
{
    Console.WriteLine("usage: metadata_search <assembly> [substring]");
    return 1;
}

string target = args.Length > 1 ? args[1] : "LoadSaveGame";
using var stream = File.OpenRead(args[0]);
using var pe = new PEReader(stream);
var reader = pe.GetMetadataReader();

int matches = 0;
foreach (var methodHandle in reader.MethodDefinitions)
{
    var method = reader.GetMethodDefinition(methodHandle);
    if (method.RelativeVirtualAddress == 0)
        continue;

    byte[] il;
    try
    {
        il = pe.GetMethodBody(method.RelativeVirtualAddress).GetILBytes();
    }
    catch
    {
        continue;
    }

    var strings = ExtractUserStrings(il, reader);
    if (!strings.Any(s => s.Contains(target, StringComparison.OrdinalIgnoreCase)))
        continue;

    var type = reader.GetTypeDefinition(method.GetDeclaringType());
    string typeName = $"{reader.GetString(type.Namespace)}.{reader.GetString(type.Name)}";
    Console.WriteLine($"== {typeName}::{reader.GetString(method.Name)} (0x{MetadataTokens.GetToken(methodHandle):X8})");
    foreach (var s in strings)
        Console.WriteLine("   \"" + s.Replace("\r", "\r").Replace("\n", "\n") + "\"");
    Console.WriteLine();
    matches++;
}

Console.WriteLine($"matches={matches}");
return 0;

static List<string> ExtractUserStrings(byte[] il, MetadataReader reader)
{
    var result = new List<string>();
    for (int i = 0; i + 4 < il.Length; i++)
    {
        if (il[i] != 0x72) // ldstr
            continue;
        int token = BitConverter.ToInt32(il, i + 1);
        if ((token & 0xFF000000) != 0x70000000)
            continue;
        try
        {
            var handle = MetadataTokens.UserStringHandle(token & 0x00FFFFFF);
            string value = reader.GetUserString(handle);
            if (!string.IsNullOrEmpty(value))
                result.Add(value);
        }
        catch
        {
            // 不是有效的用户字符串句柄，忽略。
        }
    }
    return result;
}
