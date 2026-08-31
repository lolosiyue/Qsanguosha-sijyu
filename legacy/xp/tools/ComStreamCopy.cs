using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

public static class QSanComStreamCopy
{
    private static readonly Guid IStreamId =
        new Guid("0000000c-0000-0000-C000-000000000046");

    public static void CopyToFile(object source, string destination, long length)
    {
        IntPtr unknown = Marshal.GetIUnknownForObject(source);
        IntPtr streamPointer = IntPtr.Zero;
        IntPtr bytesReadPointer = IntPtr.Zero;
        FileStream output = null;
        try
        {
            Guid interfaceId = IStreamId;
            int result = Marshal.QueryInterface(unknown, ref interfaceId, out streamPointer);
            if (result != 0)
                Marshal.ThrowExceptionForHR(result);

            IStream input = (IStream)Marshal.GetObjectForIUnknown(streamPointer);
            output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write);
            bytesReadPointer = Marshal.AllocHGlobal(sizeof(int));
            byte[] buffer = new byte[1024 * 1024];
            long remaining = length;
            while (remaining > 0)
            {
                int requested = (int)Math.Min(buffer.Length, remaining);
                Marshal.WriteInt32(bytesReadPointer, 0);
                input.Read(buffer, requested, bytesReadPointer);
                int read = Marshal.ReadInt32(bytesReadPointer);
                if (read <= 0)
                    throw new EndOfStreamException("Unexpected end of the generated ISO stream.");
                output.Write(buffer, 0, read);
                remaining -= read;
            }
        }
        finally
        {
            if (output != null)
                output.Dispose();
            if (bytesReadPointer != IntPtr.Zero)
                Marshal.FreeHGlobal(bytesReadPointer);
            if (streamPointer != IntPtr.Zero)
                Marshal.Release(streamPointer);
            Marshal.Release(unknown);
        }
    }
}
