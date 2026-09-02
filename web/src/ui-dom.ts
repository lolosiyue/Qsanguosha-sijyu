export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attrs: Record<string, string> = {},
  children: (Node | string)[] = []
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (key === "class")
      node.className = value;
    else
      node.setAttribute(key, value);
  }
  for (const child of children)
    node.append(child);
  return node;
}

export function hasCommand(command: number, ids: number[]): boolean {
  return ids.includes(command);
}
