t179=timeSeries2('C:\gridDyn\transmission\build\gridDynMain\dynfault-vis.dat');

[res,tx,adata]=sheetread('C:\Users\top1\Documents\MATLAB\bus_WECC.csv');

lat=33.5:0.05:56.2;
lon=-124:0.05:-107;

[X,Y]=meshgrid(lon,lat);

yd=res(2:180,3);
xd=res(2:180,4);

adata=diff(t179(:,181:181+178))*100/2/pi;
warning off;

%%
vidObj = VideoWriter('WECC_fault3','MPEG-4');
vidObj.FrameRate = 25;
open(vidObj);

bound=0.033;
for kk=97:401

    F = scatteredInterpolant(xd,yd,adata(kk,:)','natural','none');
vq = F(X,Y);
figure(1);
hold off;
s=pcolor(lon,lat,vq);
set(s,'LineStyle','none','FaceAlpha',0.9);
 title(sprintf('t=%f',t179.time(kk)));
 hold on;
 drawWECC('r');
 axis([-125,-102,31,max(lat),-bound,0.005,-bound,0.005]);
 axis square

writeVideo(vidObj, getframe(gcf));
end

close(vidObj);
