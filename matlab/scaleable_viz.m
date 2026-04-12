t2=timeSeries2('C:\gridDyn\transmission\build\test\scalable.dat');

N=50;
aplot=zeros(N,N,length(t2.time)-1);
for rr=1:N
    for cc=1:N
        aplot(rr,cc,:)=diff(t2(N^2+(rr-1)*N+cc))/0.02;
    end
end


vidObj = VideoWriter('freqPlotOsc1Hz_OC','MPEG-4');
vidObj.FrameRate = 25;
open(vidObj);

jj=25;
figure(1);

while(t2.time(jj)<20.0)
    mn=mean(mean(aplot(:,:,jj)));
    mesh(squeeze(aplot(:,:,jj)));
    axis([0,50,0,50,-0.01,0.01,-0.01,0.01]);
    title(sprintf('t=%f',t2.time(jj+1)));
    drawnow
   writeVideo(vidObj, getframe(gcf));
   jj=jj+1;
end

close(vidObj);
winopen('freqPlotOsc1Hz_OC.mp4')
