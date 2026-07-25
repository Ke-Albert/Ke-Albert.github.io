---
layout: page
title: 归档
permalink: /archive/
jekyll-theme-WuK:
  default:
    sidebar:
      open: true
  archive:
    group_by: "%b %Y"
    vega_lite:
      enable: true
---

已写下文字 {{ site.posts.size }} 篇，长路漫漫！

```vega-lite
{
  "data": { "url": "{{ site.baseurl }}/assets/simple-jekyll-search/search.json" },
  "encoding": {
    "y": {"field": "date", "timeUnit": "month", "type": "ordinal"},
    "x": {"field": "date", "timeUnit": "year"},
    "color": {"field": "date", "aggregate": "count"}
  },
  "mark": "rect"
}
```

{% assign posts = site.posts | sort: 'date' | reverse %}
{% assign i = 0 %}
{% for post in posts %}
{% assign year = post.date | date: page.jekyll-theme-WuK.archive.group_by %}
{% assign nyear = post.next.date | date: page.jekyll-theme-WuK.archive.group_by %}
{% if year != nyear %}

## {{ year }}{% assign i = i | plus: 1 %}

{% endif %}
- [{{ post.title }}]({{ post.url | relative_url }})
{% endfor %}
