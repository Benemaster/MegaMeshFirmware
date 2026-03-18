import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.File;
import java.util.List;

publc class ImageCompressor {
i
    public static String compressImages(List<File> imageFiles) {
        StringBuilder result = new StringBuilder();
        for (File file : imageFiles) {
            try {
                BufferedImage image = ImageIO.read(file);
                result.append(compressBufferedImage(image));
            } catch (Exception e) {
                // skip
            }
        }
        return result.toString();
    }

    private static String compressBufferedImage(BufferedImage image) {
        int w = image.getWidth();
        int h = image.getHeight();
        int[] thresholds = calculateAdaptiveThresholds(image);

        StringBuilder sb = new StringBuilder();
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int pix = image.getRGB(x, y);
                int r = (pix >> 16) & 0xFF;
                int g = (pix >> 8) & 0xFF;
                int b = pix & 0xFF;
                int gray = (int) (0.299 * r + 0.587 * g + 0.114 * b);
                int value = get2BitValue(gray, thresholds);
                sb.append(Integer.toBinaryString(value));
            }
        }
        return sb.toString();
    }

    private static int[] calculateAdaptiveThresholds(BufferedImage image) {
        int w = image.getWidth();
        int h = image.getHeight();
        int[] histogram = new int[256];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int pix = image.getRGB(x, y);
                int r = (pix >> 16) & 0xFF;
                int g = (pix >> 8) & 0xFF;
                int b = pix & 0xFF;
                int gray = (int) (0.299 * r + 0.587 * g + 0.114 * b);
                histogram[gray]++;
            }
        }
        int total = w * h;
        int[] thresholds = new int[3];
        int count = 0;
        for (int i = 0, t = 0; i < 256 && t < 3; i++) {
            count += histogram[i];
            if (count >= (total * (t + 1)) / 4) {
                thresholds[t++] = i;
            }
        }
        return thresholds;
    }

    private static int get2BitValue(int gray, int[] thresholds) {
        if (gray < thresholds[0]) return 0;
        else if (gray < thresholds[1]) return 1;
        else if (gray < thresholds[2]) return 2;
        else return 3;
    }
}