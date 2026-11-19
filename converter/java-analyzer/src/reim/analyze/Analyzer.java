package reim.analyze;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * S1 offline analyzer (M1-B / B2).
 *
 * Reads a Minecraft client JAR and produces deterministic class/dependency
 * reports. It uses only the JDK; no ASM, no decompiler, no Minecraft classes
 * are loaded. The output contains no timestamps so repeated runs over the same
 * input are byte-identical.
 *
 * Usage:
 *   java -jar reim-analyze.jar --jar <client.jar> --out <dir> --profile 26.3
 */
public final class Analyzer {
  private Analyzer() {}

  public static void main(String[] args) {
    String jar = null;
    String out = null;
    String profile = "26.3";
    for (int i = 0; i < args.length; ++i) {
      switch (args[i]) {
        case "--jar" -> {
          if (++i >= args.length) usage("missing --jar value");
          jar = args[i];
        }
        case "--out" -> {
          if (++i >= args.length) usage("missing --out value");
          out = args[i];
        }
        case "--profile" -> {
          if (++i >= args.length) usage("missing --profile value");
          profile = args[i];
        }
        case "--help", "-h" -> {
          usage(null);
          return;
        }
        default -> usage("unknown argument: " + args[i]);
      }
    }
    if (jar == null || out == null) usage("--jar and --out are required");
    try {
      run(Paths.get(jar), Paths.get(out), profile);
    } catch (Exception e) {
      System.err.println("reim-analyze: " + e.getMessage());
      System.exit(1);
    }
  }

  private static void usage(String message) {
    if (message != null) System.err.println("reim-analyze: " + message);
    System.err.println(
        "usage: reim-analyze.jar --jar <client.jar> --out <dir> [--profile 26.3]");
    if (message != null) System.exit(2);
  }

  private static void run(Path jarPath, Path outDir, String profile) throws Exception {
    if (!Files.isRegularFile(jarPath)) {
      throw new IOException("client JAR not found: " + jarPath);
    }
    Files.createDirectories(outDir.resolve("analysis"));

    final JarDigest jarDigest = digestFile(jarPath);
    final List<ClassInfo> classes = new ArrayList<>();
    final Map<String, Integer> dependencyRefs = new TreeMap<>();
    final List<String> parseErrors = new ArrayList<>();
    long methodTotal = 0;
    long fieldTotal = 0;

    try (ZipFile zip = new ZipFile(jarPath.toFile())) {
      final Enumeration<? extends ZipEntry> entries = zip.entries();
      while (entries.hasMoreElements()) {
        final ZipEntry entry = entries.nextElement();
        final String name = entry.getName();
        if (entry.isDirectory() || !name.endsWith(".class")) continue;
        if (name.startsWith("META-INF/")) continue;
        try (InputStream stream = zip.getInputStream(entry)) {
          final byte[] bytes = stream.readAllBytes();
          final ClassInfo info = ClassParser.parse(bytes);
          if (info == null) {
            parseErrors.add(name + ": unsupported class file");
            continue;
          }
          info.entryName = name;
          classes.add(info);
          methodTotal += info.methods;
          fieldTotal += info.fields;
          for (String dep : info.dependencies) {
            dependencyRefs.merge(dep, 1, Integer::sum);
          }
        } catch (RuntimeException e) {
          parseErrors.add(name + ": " + e.getMessage());
        }
      }
    }

    classes.sort(Comparator.comparing(a -> a.name));

    final Map<String, Integer> kinds = new TreeMap<>();
    kinds.put("minecraft", 0);
    kinds.put("jdk", 0);
    kinds.put("third_party", 0);
    for (String dep : dependencyRefs.keySet()) {
      kinds.merge(classify(dep), 1, Integer::sum);
    }

    final StringBuilder classesJson = new StringBuilder(classes.size() * 180 + 512);
    classesJson.append("{\n");
    classesJson.append("  \"format\": 1,\n");
    classesJson.append("  \"profile\": ").append(json(profile)).append(",\n");
    classesJson.append("  \"jar\": {\"path\": ").append(json(jarPath.toAbsolutePath().toString()))
        .append(", \"sha256\": ").append(json(jarDigest.sha256))
        .append(", \"size\": ").append(jarDigest.size).append("},\n");
    classesJson.append("  \"class_count\": ").append(classes.size()).append(",\n");
    classesJson.append("  \"method_count\": ").append(methodTotal).append(",\n");
    classesJson.append("  \"field_count\": ").append(fieldTotal).append(",\n");
    classesJson.append("  \"hash_scheme\": \"sha256 over each method: name, NUL, descriptor, NUL, code bytes; declaration order\",\n");
    classesJson.append("  \"parse_error_count\": ").append(parseErrors.size()).append(",\n");
    classesJson.append("  \"parse_errors\": [");
    for (int i = 0; i < parseErrors.size(); ++i) {
      if (i > 0) classesJson.append(", ");
      classesJson.append(json(parseErrors.get(i)));
    }
    classesJson.append("],\n");
    classesJson.append("  \"classes\": [\n");
    for (int i = 0; i < classes.size(); ++i) {
      final ClassInfo info = classes.get(i);
      classesJson.append("    {\"name\": ").append(json(info.name))
          .append(", \"super\": ").append(info.superName == null ? "null" : json(info.superName))
          .append(", \"interfaces\": [");
      for (int j = 0; j < info.interfaces.size(); ++j) {
        if (j > 0) classesJson.append(", ");
        classesJson.append(json(info.interfaces.get(j)));
      }
      classesJson.append("], \"methods\": ").append(info.methods)
          .append(", \"fields\": ").append(info.fields)
          .append(", \"bytecode_sha256\": ").append(json(info.bytecodeHash))
          .append("}");
      classesJson.append(i + 1 == classes.size() ? "\n" : ",\n");
    }
    classesJson.append("  ]\n}\n");

    final StringBuilder depsJson = new StringBuilder(dependencyRefs.size() * 80 + 512);
    depsJson.append("{\n");
    depsJson.append("  \"format\": 1,\n");
    depsJson.append("  \"profile\": ").append(json(profile)).append(",\n");
    depsJson.append("  \"dependency_count\": ").append(dependencyRefs.size()).append(",\n");
    depsJson.append("  \"by_kind\": {");
    int kindIndex = 0;
    for (Map.Entry<String, Integer> entry : kinds.entrySet()) {
      if (kindIndex++ > 0) depsJson.append(", ");
      depsJson.append(json(entry.getKey())).append(": ").append(entry.getValue());
    }
    depsJson.append("},\n");
    depsJson.append("  \"dependencies\": [\n");
    int depIndex = 0;
    for (Map.Entry<String, Integer> entry : dependencyRefs.entrySet()) {
      depsJson.append("    {\"name\": ").append(json(entry.getKey()))
          .append(", \"kind\": ").append(json(classify(entry.getKey())))
          .append(", \"refs\": ").append(entry.getValue()).append("}");
      depsJson.append(++depIndex == dependencyRefs.size() ? "\n" : ",\n");
    }
    depsJson.append("  ]\n}\n");

    Files.writeString(outDir.resolve("analysis").resolve("classes.json"),
        classesJson.toString(), StandardCharsets.UTF_8);
    Files.writeString(outDir.resolve("analysis").resolve("deps.json"),
        depsJson.toString(), StandardCharsets.UTF_8);

    System.out.println("reim-analyze: classes=" + classes.size()
        + " methods=" + methodTotal
        + " fields=" + fieldTotal
        + " deps=" + dependencyRefs.size()
        + " parse_errors=" + parseErrors.size());
    System.out.println("reim-analyze: wrote " + outDir.resolve("analysis"));
  }

  private static String classify(String internalName) {
    if (internalName.startsWith("net/minecraft/")) return "minecraft";
    if (internalName.startsWith("java/") || internalName.startsWith("javax/")
        || internalName.startsWith("jdk/") || internalName.startsWith("sun/")) {
      return "jdk";
    }
    return "third_party";
  }

  private static JarDigest digestFile(Path path) throws IOException, NoSuchAlgorithmException {
    final MessageDigest digest = MessageDigest.getInstance("SHA-256");
    long size = 0;
    try (InputStream in = Files.newInputStream(path);
         DigestInputStream stream = new DigestInputStream(in, digest)) {
      final byte[] buffer = new byte[1 << 16];
      int read;
      while ((read = stream.read(buffer)) > 0) size += read;
    }
    return new JarDigest(size, hex(digest.digest()));
  }

  private static String hex(byte[] bytes) {
    final StringBuilder out = new StringBuilder(bytes.length * 2);
    for (byte b : bytes) out.append(String.format("%02x", b));
    return out.toString();
  }

  private static String json(String value) {
    final StringBuilder out = new StringBuilder(value.length() + 2);
    out.append('"');
    for (int i = 0; i < value.length(); ++i) {
      final char c = value.charAt(i);
      switch (c) {
        case '"' -> out.append("\\\"");
        case '\\' -> out.append("\\\\");
        case '\b' -> out.append("\\b");
        case '\f' -> out.append("\\f");
        case '\n' -> out.append("\\n");
        case '\r' -> out.append("\\r");
        case '\t' -> out.append("\\t");
        default -> {
          if (c < 0x20) out.append(String.format("\\u%04x", (int) c));
          else out.append(c);
        }
      }
    }
    out.append('"');
    return out.toString();
  }

  private record JarDigest(long size, String sha256) {}

  private static final class ClassInfo {
    String name;
    String entryName;
    String superName;
    final List<String> interfaces = new ArrayList<>();
    int methods;
    int fields;
    String bytecodeHash;
    final List<String> dependencies = new ArrayList<>();
  }

  /** Minimal, allocation-light class-file reader for names, counts and code. */
  private static final class ClassParser {
    private final byte[] data;
    private int position;

    private ClassParser(byte[] data) {
      this.data = data;
    }

    static ClassInfo parse(byte[] data) {
      final ClassParser parser = new ClassParser(data);
      return parser.parseClass();
    }

    private ClassInfo parseClass() {
      if (u4() != 0xCAFEBABEL) throw new IllegalStateException("bad magic");
      u2(); // minor
      u2(); // major
      final int constantCount = u2();
      final String[] utf8 = new String[constantCount];
      final String[] className = new String[constantCount];
      final int[] classNameIndex = new int[constantCount];
      final List<Integer> refClassIndexes = new ArrayList<>();

      for (int i = 1; i < constantCount; ++i) {
        final int tag = u1();
        switch (tag) {
          case 1 -> utf8[i] = readUtf8();
          case 3, 4 -> position += 4;
          case 5, 6 -> {
            position += 8;
            ++i; // long/double take two slots
          }
          case 7 -> classNameIndex[i] = u2();
          case 8 -> position += 2;
          case 9, 10, 11 -> {
            final int classIndex = u2();
            final int natIndex = u2();
            refClassIndexes.add(classIndex);
            if (natIndex > 0 && natIndex < constantCount) {
              // name/type is parsed in another slot; no action needed here.
            }
          }
          case 12 -> position += 4;
          case 15 -> position += 3;
          case 16 -> position += 2;
          case 17, 18 -> position += 4;
          case 19, 20 -> position += 2;
          default -> throw new IllegalStateException("unknown constant tag " + tag);
        }
      }

      // Resolve Class entries only after the whole constant pool is read:
      // a Class entry may reference a Utf8 entry that appears later.
      for (int i = 1; i < constantCount; ++i) {
        if (classNameIndex[i] > 0) {
          className[i] = utf8At(utf8, classNameIndex[i]);
        }
      }

      u2(); // access flags
      final int thisClass = u2();
      final int superClass = u2();
      final int interfaceCount = u2();
      final List<String> interfaces = new ArrayList<>(interfaceCount);
      for (int i = 0; i < interfaceCount; ++i) {
        final int index = u2();
        if (index > 0 && index < constantCount && className[index] != null) {
          interfaces.add(className[index]);
        }
      }
      final int fieldCount = u2();
      for (int i = 0; i < fieldCount; ++i) {
        u2();
        u2();
        u2();
        skipAttributes();
      }
      final int methodCount = u2();
      final MessageDigest methodDigest = sha256();
      final String name = thisClass > 0 && thisClass < constantCount ? className[thisClass] : null;
      for (int i = 0; i < methodCount; ++i) {
        u2(); // access
        final int methodName = u2();
        final int methodDescriptor = u2();
        final String methodNameText = utf8At(utf8, methodName);
        final String descriptorText = utf8At(utf8, methodDescriptor);
        update(methodDigest, methodNameText);
        methodDigest.update((byte) 0);
        update(methodDigest, descriptorText);
        methodDigest.update((byte) 0);

        final int attributeCount = u2();
        for (int a = 0; a < attributeCount; ++a) {
          final int attributeName = u2();
          final long length = u4();
          final long end = position + length;
          if ("Code".equals(utf8At(utf8, attributeName))) {
            position += 4; // max_stack, max_locals
            final long codeLength = u4();
            if (codeLength < 0 || position + codeLength > data.length) {
              throw new IllegalStateException("bad Code length");
            }
            methodDigest.update(data, position, (int) codeLength);
            position += (int) codeLength;
            final int exceptionCount = u2();
            position += exceptionCount * 8;
            final int codeAttributes = u2();
            for (int ca = 0; ca < codeAttributes; ++ca) {
              u2();
              final long innerLength = u4();
              position += (int) innerLength;
            }
            if (position != end) position = (int) end;
          } else {
            position = (int) end;
          }
          if (position > data.length) throw new IllegalStateException("attribute overflow");
        }
      }
      skipAttributes();

      final ClassInfo info = new ClassInfo();
      info.name = name;
      info.superName = superClass > 0 && superClass < constantCount ? className[superClass] : null;
      info.interfaces.addAll(interfaces);
      info.methods = methodCount;
      info.fields = fieldCount;
      info.bytecodeHash = hex(methodDigest.digest());
      final List<String> deps = new ArrayList<>();
      for (String value : className) {
        if (value != null) deps.add(value);
      }
      for (int index : refClassIndexes) {
        if (index > 0 && index < constantCount && className[index] != null) {
          deps.add(className[index]);
        }
      }
      deps.sort(String::compareTo);
      String previous = null;
      for (String dep : deps) {
        if (!dep.equals(previous)) {
          info.dependencies.add(dep);
          previous = dep;
        }
      }
      return info;
    }

    private void skipAttributes() {
      final int count = u2();
      for (int i = 0; i < count; ++i) {
        u2();
        final long length = u4();
        position += (int) length;
        if (position > data.length) throw new IllegalStateException("attribute overflow");
      }
    }

    private String readUtf8() {
      final int length = u2();
      final String value = new String(data, position, length, StandardCharsets.UTF_8);
      position += length;
      return value;
    }

    private String utf8At(String[] table, int index) {
      return index > 0 && index < table.length && table[index] != null ? table[index] : "";
    }

    private static void update(MessageDigest digest, String text) {
      final byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
      digest.update(bytes, 0, bytes.length);
    }

    private static MessageDigest sha256() {
      try {
        return MessageDigest.getInstance("SHA-256");
      } catch (NoSuchAlgorithmException e) {
        throw new IllegalStateException(e);
      }
    }

    private int u1() {
      return data[position++] & 0xFF;
    }

    private int u2() {
      final int value = ((data[position] & 0xFF) << 8) | (data[position + 1] & 0xFF);
      position += 2;
      return value;
    }

    private long u4() {
      final long value = ((long) (data[position] & 0xFF) << 24)
          | ((long) (data[position + 1] & 0xFF) << 16)
          | ((long) (data[position + 2] & 0xFF) << 8)
          | (long) (data[position + 3] & 0xFF);
      position += 4;
      return value;
    }
  }
}
