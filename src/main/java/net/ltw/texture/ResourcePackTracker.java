package net.ltw.texture;

import net.minecraft.client.Minecraft;
import net.minecraft.server.packs.repository.Pack;
import net.minecraft.server.packs.repository.PackRepository;

import java.io.File;
import java.util.Collection;
import java.util.HashSet;
import java.util.Set;

/**
 * Tracks active resourcepacks and manages atlas capture mode.
 *
 * Vanilla only -> ASTC injection works (pre-compressed atlases).
 * Resourcepack active -> ASTC skipped (wrong textures), PNG used,
 *   atlas captured ONCE for later ASTC compression.
 */
public final class ResourcePackTracker {

    private static final String CAPTURE_BASE = "/sdcard/minecraft_astc/captured";
    private static final String VANILLA = "vanilla";

    private static String currentPackKey = VANILLA;
    private static boolean captureMode = false;

    public static void onAtlasUpload() {
        try {
            Minecraft mc = Minecraft.getInstance();
            if (mc == null) return;

            var field = Minecraft.class.getDeclaredField("resourcePackRepository");
            field.setAccessible(true);
            PackRepository repo = (PackRepository) field.get(mc);

            Collection<Pack> selected = repo.getSelectedPacks();
            Set<String> nonVanilla = new HashSet<>();
            for (Pack pack : selected) {
                String id = pack.getId();
                if (!id.equals(VANILLA) && !id.equals("fabric")) {
                    nonVanilla.add(id);
                }
            }

            if (nonVanilla.isEmpty()) {
                currentPackKey = VANILLA;
                captureMode = false;
            } else {
                currentPackKey = sanitizePackKey(nonVanilla);
                File captureDir = new File(CAPTURE_BASE + "/" + currentPackKey);
                captureMode = !captureDir.exists() || captureDir.list() == null || captureDir.list().length == 0;
            }
        } catch (Throwable t) {
            captureMode = false;
        }
    }

    public static boolean isVanillaOnly() { return VANILLA.equals(currentPackKey); }
    public static boolean isCaptureMode() { return captureMode; }
    public static String getPackKey() { return currentPackKey; }
    public static String getCaptureDir() { return CAPTURE_BASE + "/" + currentPackKey; }

    private static String sanitizePackKey(Set<String> packIds) {
        StringBuilder sb = new StringBuilder();
        for (String id : packIds) {
            if (sb.length() > 0) sb.append("_plus_");
            for (int i = 0; i < id.length(); i++) {
                char c = id.charAt(i);
                if (c == '/' || c == ':' || c == ' ' || c == '\\') {
                    sb.append('_');
                } else {
                    sb.append(c);
                }
            }
        }
        return sb.toString();
    }
}
