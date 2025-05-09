// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Collections;

namespace System.DirectoryServices.ActiveDirectory
{
    public class ReadOnlyStringCollection : ReadOnlyCollectionBase
    {
        internal ReadOnlyStringCollection() { }

        internal ReadOnlyStringCollection(ArrayList values)
        {
            this.InnerList.AddRange(values ?? new ArrayList());
        }
        public string this[int index]
        {
            get
            {
                object returnValue = InnerList[index]!;

                if (returnValue is Exception)
                    throw (Exception)returnValue;
                else
                    return (string)returnValue;
            }
        }

        public bool Contains(string value)
        {
            ArgumentNullException.ThrowIfNull(value);

            for (int i = 0; i < InnerList.Count; i++)
            {
                string tmp = (string)InnerList[i]!;
                if (Utils.Compare(tmp, value) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        public int IndexOf(string value)
        {
            ArgumentNullException.ThrowIfNull(value);

            for (int i = 0; i < InnerList.Count; i++)
            {
                string tmp = (string)InnerList[i]!;
                if (Utils.Compare(tmp, value) == 0)
                {
                    return i;
                }
            }
            return -1;
        }

        public void CopyTo(string[] values, int index)
        {
            InnerList.CopyTo(values, index);
        }

        internal void Add(string value) => InnerList.Add(value);
    }
}
